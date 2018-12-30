/**
 * Name:     Wake On Lan with ESP8266
 * Autor:    Alberto Gil Tesa
 * Web:      https://giltesa.com/?p=19312
 * License:  CC BY-NC-SA 3.0
 * Version:  1.0
 * Date:     2018/12/30
 *
 * Note:     Look for the words "MODIFY" to customize the code before writing it to ESP8266.
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


//#define MY_DEBUG     true                //MODIFY: Uncomment?
const char* ssid     = "MyWiFi";           //MODIFY
const char* password = "MyPassWordLalala"; //MODIFY
ESP8266WebServer server(80);

uint8_t hmacKey[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}; //MODIFY with: http://www.lucadentella.it/OTP
TOTP totp         = TOTP(hmacKey, 10);
List<String> validTokenList(4);

WiFiUDP UDP;
IPAddress broadcastIP(255,255,255,255);

List<Device> deviceList(4);



void setup()
{
    #ifdef MY_DEBUG
        Serial.begin(115200);
        while(!Serial);
        Serial.println("\n0. ESP8266 started");
    #endif

    NTP.begin();



    //CONFIGURE THE DEVICES //MODIFY: For your devices, Remember to also modify the file wol.html
    //
    byte mac1[] = { 0x00, 0x11, 0x22, 0x33, 0x44, 0x55 };
    deviceList.Add( Device("NAS", IPAddress(192, 168, 1, 2), mac1) );

    byte mac2[] = { 0x55, 0x44, 0x33, 0x22, 0x11, 0x00 };
    deviceList.Add( Device("XPC", IPAddress(192, 168, 1, 3), mac2) );



    //CONFIGURE THE NETWORK
    //
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    if( WiFi.status() != WL_CONNECTED )
    {
        delay(500);
        #ifdef MY_DEBUG
            Serial.print("1. Waiting to connect");
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
        Serial.print("2. Connected to: ");
        Serial.println(WiFi.SSID());
        Serial.print("3. IP address:   ");
        Serial.println(WiFi.localIP());
    #endif



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
        Serial.println("4. WEB file list:");
        Serial.print(str);
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
        Serial.println("5. HTTP server started");
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
           String newCode = String( totp.getCode(NTP.getTime()) );
           String token;

    if( !oldCode.equals(newCode) )
    {
        oldCode = newCode;
        token   = newCode;
    }

    return token;
}