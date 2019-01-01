/**
 * Name:     Wake On Lan with ESP8266
 * Autor:    Alberto Gil Tesa
 * Web:      https://giltesa.com/?p=19312
 * License:  CC BY-NC-SA 4.0
 */



#ifndef _Device_h
#define _Device_h

class Device
{
    public:
        Device();
        Device( const String name );
        Device( const String name, const IPAddress ip, const uint8_t *mac );
        bool operator==(const Device dev) const;

        String name;
        IPAddress ip;
        uint8_t mac[6];
};
#endif


Device::Device()
{
}

Device::Device( const String name )
{
    this->name = name;
}

Device::Device( const String name, const IPAddress ip, const uint8_t *mac )
{
    this->name = name;
    this->ip   = ip;
    memcpy(this->mac, mac, sizeof(this->mac));
}

bool Device::operator==(const Device dev) const
{
    return this->name.equalsIgnoreCase(dev.name);
}