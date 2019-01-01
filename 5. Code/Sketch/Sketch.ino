/**
 * Name:     Wake On Lan with ESP8266
 * Autor:    Alberto Gil Tesa
 * Web:      https://giltesa.com/?p=19312
 * License:  CC BY-NC-SA 4.0
 * Version:  1.0.3
 * Date:     2019/01/01
 */



#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <FS.h>                 //https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html

#include <TOTP.h>               //https://github.com/lucadentella/TOTP-Arduino
#include <sha1.h>               //Included in TOTP
#include <NtpClientLib.h>       //https://github.com/gmag11/NtpClient
#include <TimeLib.h>            //https://github.com/PaulStoffregen/Time
#include <WakeOnLan.h>          //https://github.com/koenieee/WakeOnLan-ESP8266
#include <WiFiUDP.h>
#include <ListLib.h>            //https://github.com/luisllamasbinaburo/Arduino-List
#include <ESP8266Ping.h>        //https://github.com/dancol90/ESP8266Ping
#include "Device.h"



//#define MY_DEBUG true
#define COUNT(x) sizeof(x)/sizeof(*x)

char ssid[16];
char password[16];
char hostName[16];
ESP8266WebServer server(80);

TOTP *totp;
uint8_t hmacKey[10];
List<String> validTokenList(4);

WiFiUDP UDP;
IPAddress broadcastIP(255,255,255,255);

List<Device> deviceList(4);



void setup()
{
    #ifdef MY_DEBUG
        Serial.begin(115200);
        while(!Serial);
        Serial.println("\n1. ESP8266 started");
    #endif

    NTP.begin();



    //PRINTS THE LIST OF FILES STORED IN THE FLASH MEMORY
    //
    SPIFFS.begin();

    #ifdef MY_DEBUG
        String str = "";
        Dir dir = SPIFFS.openDir("/");
        while( dir.next() )
        {
            str += "\t";
            str += dir.fileName();
            str += " / ";
            str += dir.fileSize();
            str += "\r\n";
        }
        Serial.println("2. WEB file list:");
        Serial.print(str);
    #endif



    //CONFIGURE THE ESP8266
    //
    #ifdef MY_DEBUG
        Serial.println("3. Loading configuration");
    #endif

    loadConfig("/config.txt");

    totp = new TOTP(hmacKey, COUNT(hmacKey));



    //CONFIGURE THE NETWORK
    //
    WiFi.mode(WIFI_STA);
    WiFi.hostname(hostName);
    WiFi.begin(ssid, password);

    if( WiFi.status() != WL_CONNECTED )
    {
        delay(500);
        #ifdef MY_DEBUG
            Serial.print("4. Waiting to connect");
        #endif

        while( WiFi.status() != WL_CONNECTED )
        {
            delay(500);
            #ifdef MY_DEBUG
                Serial.print(".");
            #endif
        }
    }
    #ifdef MY_DEBUG
        Serial.println();
        Serial.print("5. Connected to: ");
        Serial.println(WiFi.SSID());
        Serial.print("6. IP address:   ");
        Serial.println(WiFi.localIP());
    #endif



    //CONFIGURE WEB SERVICE HANDLERS
    //
    server.on("/",           handleRoot);
    server.on("/index.html", handleRoot);

    server.on("/ws", handleWebService);

    server.onNotFound([]()
    {
        String uri = server.uri();

        if( !uri.equals("/login.html") && !uri.equals("/wol.html") )
        {
            if( !handleFileRead(uri) ){
                server.send(404, "text/plain", "404: Not Found");
            }
        }
    });

    const char* headerKeys[] = { "Cookie" };
    size_t headerKeysSize    = sizeof(headerKeys) / sizeof(char*);
    server.collectHeaders(headerKeys, headerKeysSize);

    server.begin();

    #ifdef MY_DEBUG
        Serial.println("7. HTTP server started");
    #endif


    pinMode(LED_BUILTIN, OUTPUT);
    for( int i=0 ; i < 3 ; i++ )
    {
        digitalWrite(LED_BUILTIN, LOW);
        delay(600);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(400);
    }
}



void loop()
{
    server.handleClient();

    while( WiFi.status() != WL_CONNECTED || WiFi.localIP() == IPAddress(0,0,0,0) )
    {
        WiFi.reconnect();
        delay(1000);
    }
}



/**
 * Root page can be accessed only if authentification is ok
 */
void handleRoot()
{
    #ifdef MY_DEBUG
        Serial.println("Enter handleRoot");
    #endif

    handleFileRead(!isAuthentified() ? "/login.html" : "/wol.html");
    server.send(200);
}



/**
 * Handler of Ajax connections.
 */
void handleWebService()
{
    #ifdef MY_DEBUG
        Serial.println("Enter handle WebService");
    #endif



    // WS: For login
    //
    if( server.hasArg("action") && server.arg("action").equals("login") )
    {
        String token = server.arg("password");

        if( token.equals(getTokenTOTP()) )
        {
            #ifdef MY_DEBUG
                Serial.println("Login Successful");
            #endif

            if( validTokenList.Count() >= validTokenList.Capacity() ){
                validTokenList.Clear();
            }
            validTokenList.Add(token);

            server.sendHeader("Location", "/");
            server.sendHeader("Cache-Control", "no-cache");
            server.sendHeader("Set-Cookie", String("ESPSESSIONID="+token));
            server.send(301);
        }
        else
        {
            #ifdef MY_DEBUG
                Serial.println("Login Failed");
            #endif

            server.send(401, "text/plain", "401: Unauthorized");
        }
    }



    // WS: Returns the list of devices.
    //
    if( server.hasArg("action") && server.arg("action").equals("devices") )
    {
        if( !isAuthentified() )
        {
            #ifdef MY_DEBUG
                Serial.println("Login Failed");
            #endif

            server.send(401, "text/plain", "401: Unauthorized");
        }
        else
        {
            if( deviceList.Count() > 0 )
            {
                String index, name, json = "{";

                for( int i=0 ; i < deviceList.Count(); i++ )
                {
                    index = String(i);
                    name  = String(deviceList[i].name);
                    json += ("\""+ index +"\":\""+ name +"\"");
                    json += (i < deviceList.Count()-1 ? ",":"");
                }

                json += "}";

                server.send(200, "application/json", json);
            }
            else
            {
                server.send(400, "text/plain", "400: Bad Request");
            }
        }
    }



    // WS: Check if the device is really turn on.
    //
    if( server.hasArg("action") && server.arg("action").equals("ping") )
    {
        if( !isAuthentified() )
        {
            #ifdef MY_DEBUG
                Serial.println("Login Failed");
            #endif

            server.send(401, "text/plain", "401: Unauthorized");
        }
        else
        {
            String name = server.arg("device");
            int index   = deviceList.IndexOf( Device(name) );

            if( index != -1 )
            {
                server.send(200, "text/plain", Ping.ping(deviceList[index].ip) ? "true" : "false");
            }
            else
            {
                server.send(400, "text/plain", "400: Bad Request");
            }
        }
    }



    // WS: Turn on the device by Wake On Lan.
    //
    if( server.hasArg("action") && server.arg("action").equals("wol") )
    {
        if( !isAuthentified() )
        {
            #ifdef MY_DEBUG
                Serial.println("Login Failed");
            #endif

            server.send(401, "text/plain", "401: Unauthorized");
        }
        else
        {
            String name = server.arg("device");
            int index   = deviceList.IndexOf( Device(name) );

            if( index != -1 )
            {
                WakeOnLan::sendWOL(broadcastIP, UDP, deviceList[index].mac, sizeof deviceList[index].mac);
                server.send(200, "text/plain", "The "+ name +" has been turned on.");
            }
            else
            {
                server.send(400, "text/plain", "400: Bad Request");
            }

            //Delete the used token:
            server.sendHeader("Set-Cookie", "ESPSESSIONID=0");
            validTokenList.Remove( validTokenList.IndexOf( getTokenFromCookie() ) );
        }
    }

}



/**
 * Send the right file to the client (if it exists)
 * https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
 */
bool handleFileRead( String path )
{
    String contentType = getContentType(path);
    String pathWithGz  = path + ".gz";

    if( SPIFFS.exists(pathWithGz) || SPIFFS.exists(path) )
    {
        if( SPIFFS.exists(pathWithGz) )                         // If there's a compressed version available
            path += ".gz";                                      // Use the compressed version

        File file   = SPIFFS.open(path, "r");
        size_t sent = server.streamFile(file, contentType);
        file.close();

        return true;
    }

    #ifdef MY_DEBUG
        Serial.println(String("\tFile Not Found: ") + path);
    #endif

    return false;
}



/**
 * Convert the file extension to the MIME type
 */
String getContentType(String filename)
{
    if(filename.endsWith(".htm"))       return "text/html";
    else if(filename.endsWith(".html")) return "text/html";
    else if(filename.endsWith(".css"))  return "text/css";
    else if(filename.endsWith(".js"))   return "application/javascript";
    else if(filename.endsWith(".svg"))  return "image/svg+xml";
    else if(filename.endsWith(".png"))  return "image/png";
    else if(filename.endsWith(".gif"))  return "image/gif";
    else if(filename.endsWith(".jpg"))  return "image/jpeg";
    else if(filename.endsWith(".ico"))  return "image/x-icon";
    else if(filename.endsWith(".xml"))  return "text/xml";
    else if(filename.endsWith(".pdf"))  return "application/x-pdf";
    else if(filename.endsWith(".zip"))  return "application/x-zip";
    else if(filename.endsWith(".gz"))   return "application/x-gzip";

    return "text/plain";
}



/**
 * Check if the user cookie, which contains the token, is a valid token.
 */
bool isAuthentified()
{
    #ifdef MY_DEBUG
        Serial.println("Enter isAuthentified");
    #endif

    bool logged = validTokenList.Contains( getTokenFromCookie() );

    #ifdef MY_DEBUG
        Serial.println(logged ? "Authentification Successful" : "Authentification Failed");
    #endif

    return logged;
}



/**
 * Retrieve the session cookie sent to the client beforehand.
 * The cookie stores the token used to login.
 */
String getTokenFromCookie()
{
    String token;

    #ifdef MY_DEBUG
        Serial.println("Enter getTokenFromCookie");
    #endif


    if( server.hasHeader("Cookie") )
    {
        String cookie = server.header("Cookie");

        int index = cookie.indexOf("=");
        if( index != -1 )
        {
            token = cookie.substring(index+1);
        }
    }

    return token;
}



/**
 * Generate Time-based One-Time Passwords.
 *
 * http://www.lucadentella.it/en/2013/09/14/serratura-otp
 * https://github.com/lucadentella/TOTP-Arduino
 */
String getTokenTOTP()
{
    #ifdef MY_DEBUG
        Serial.println("Enter getTokenTOTP");
    #endif

    static String oldCode = "";
           String newCode = String( totp->getCode(NTP.getTime()) );
           String token;

    if( !oldCode.equals(newCode) )
    {
        oldCode = newCode;
        token   = newCode;
    }

    return token;
}



/**
 * Read the necessary data to configure the ESP8266.
 */
void loadConfig( String path )
{
    if( SPIFFS.exists(path) )
    {
        File file = SPIFFS.open(path, "r");
        String line;

        if( file )
        {
            bool flagConf = false;

            while( file.available() > 0 )
            {
                if( file.peek() == '\r' || file.peek() == '#' ){
                    file.readStringUntil('\n');
                    continue;
                }

                if( !flagConf )
                {
                    //Read basic configuration:
                    flagConf = true;


                    //SSID
                    line = file.readStringUntil('\n');
                    line.toCharArray(ssid, line.length());

                    #ifdef MY_DEBUG
                        Serial.print("SSID     = ");
                        Serial.println(ssid);
                    #endif


                    //PASSWORD
                    line = file.readStringUntil('\n');
                    line.toCharArray(password, line.length());

                    #ifdef MY_DEBUG
                        Serial.print("PASSWORD = ");
                        Serial.println(password);
                    #endif


                    //HOSTNAME
                    line = file.readStringUntil('\n');
                    line.toCharArray(hostName, line.length());

                    #ifdef MY_DEBUG
                        Serial.print("HOSTNAME = ");
                        Serial.println(hostName);
                    #endif


                    //TOTP KEY
                    char tArray[3];
                    for( int i=0 ; i<COUNT(hmacKey) ; i++ )
                    {
                        line = file.readStringUntil(i<COUNT(hmacKey)-1 ? ' ' : '\n');
                        strcpy(tArray, line.c_str());
                        hmacKey[i] = (byte)strtol(tArray, NULL, 16);
                    }

                    #ifdef MY_DEBUG
                        Serial.print("TOTP KEY = ");
                        for( int i=0 ; i<COUNT(hmacKey) ; i++ ){
                            Serial.print(hmacKey[i], HEX);
                            Serial.print(i<COUNT(hmacKey)-1 ? " " : "");
                        }
                        Serial.println();
                    #endif
                }
                else
                {
                    //Read devices:

                    //Name Device:
                    String name = file.readStringUntil('\n');
                    name.trim();

                    #ifdef MY_DEBUG
                        Serial.print("NAME = ");
                        Serial.println(name);
                    #endif


                    //IP:
                    IPAddress ip(file.parseInt(), file.parseInt(), file.parseInt(), file.parseInt());

                    #ifdef MY_DEBUG
                        Serial.print("IP   = ");
                        Serial.println(ip);
                    #endif


                    //MAC:
                    byte mac[6];
                    char tArray[3];
                    for( int i=0 ; i<COUNT(mac) ; i++ )
                    {
                        line = file.readStringUntil(i<COUNT(mac)-1 ? ':' : '\n');
                        strcpy(tArray, line.c_str());
                        mac[i] = (byte)strtol(tArray, NULL, 16);
                    }

                    #ifdef MY_DEBUG
                        Serial.print("MAC  = ");
                        for( int i=0 ; i<COUNT(mac) ; i++ ){
                            Serial.print(mac[i], HEX);
                            Serial.print(i<COUNT(mac)-1 ? ":" : "");
                        }
                        Serial.println();
                    #endif


                    deviceList.Add( Device(name, ip, mac) );
                }
            }
        }

        file.close();
    }
}