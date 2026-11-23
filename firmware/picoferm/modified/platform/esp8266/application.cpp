// Complete modified PicoFerm owner. No OEM cloud controller, HTTP listener,
// sleep scheduler or unbounded ADC wait is instantiated in this profile.
#include "picoferm/standalone.hpp"
#include "picoferm/local_controls.hpp"
#include "picobrew/provisioning.hpp"
#include "picobrew/common_web.h"
#include "storage.hpp"
#include "telemetry.hpp"
#include "http.hpp"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <Wire.h>
#include <cstdio>
#include <cstring>
#ifdef word
#undef word
#endif

namespace picoferm { namespace standalone { namespace {
namespace api=picobrew::standalone;
namespace sdk=picobrew::standalone::esp8266;
class Hardware final:public oem::HardwarePorts {
public:
    std::uint32_t millis() override{return ::millis();}
    void pin_mode(unsigned pin,unsigned mode) override{::pinMode(pin,mode);}
    std::int32_t analog_read(unsigned pin) override{return ::analogRead(pin);}
    void wire_begin(unsigned sda,unsigned scl) override{Wire.begin(sda,scl);}
    void wire_begin_transmission(unsigned address) override{Wire.beginTransmission(static_cast<std::uint8_t>(address));}
    void wire_write(std::uint8_t value) override{Wire.write(value);}
    void wire_end_transmission() override{Wire.endTransmission();}
    void wire_request(unsigned address,unsigned count) override{Wire.requestFrom(static_cast<std::uint8_t>(address),static_cast<std::uint8_t>(count));}
    std::uint32_t wire_read() override{return static_cast<std::uint32_t>(Wire.read());}
};
class Diagnostics final:public oem::Ports {
public:
    bool forbidden_write=false;
    void yield() override{::yield();}
    void text(const char* scope,const char* key,const char* value) override{
        // Only recovered LED/button lifecycle messages use this port. Physical
        // provisioning responses have their own input-free message boundary.
        char line[128];::snprintf(line,sizeof(line),"%.12s %.24s: %.72s",scope?scope:"",key?key:"",value?value:"");Serial.println(line);
    }
    void integer(const char* scope,const char* key,std::uint32_t value) override{char bytes[16];::snprintf(bytes,sizeof(bytes),"%lu",static_cast<unsigned long>(value));text(scope,key,bytes);}
    void number(const char* scope,const char* key,float value) override{char bytes[48];::dtostrf(value,0,3,bytes);text(scope,key,bytes);}
    void boolean(const char* scope,const char* key,bool value) override{text(scope,key,value?"true":"false");}
    void digital_write(unsigned pin,bool value) override{::digitalWrite(pin,value?HIGH:LOW);}
    std::uint32_t digital_read(unsigned pin) override{return ::digitalRead(pin);}
    void persist(const std::uint8_t*,std::size_t) override{
        forbidden_write=true;Serial.println("ERROR: OEM EEPROM writes are unavailable in standalone mode");
    }
};
class CheckedSensors {
public:
    SensorIo binding(){SensorIo io;io.context=this;
        io.begin=[](void* p,unsigned sda,unsigned scl,unsigned address){auto& self=*static_cast<CheckedSensors*>(p);
            self.address_=static_cast<std::uint8_t>(address);Wire.begin(sda,scl);Wire.setClockStretchLimit(5000);return true;};
        io.read=[](void* p,std::uint8_t reg,std::uint8_t& value){return static_cast<CheckedSensors*>(p)->read(reg,&value,1);};
        io.write=[](void* p,std::uint8_t reg,std::uint8_t value){auto& self=*static_cast<CheckedSensors*>(p);Wire.beginTransmission(self.address_);
            const bool copied=Wire.write(reg)==1&&Wire.write(value)==1;const auto status=Wire.endTransmission();return copied&&status==0;};
        io.read24=[](void* p,std::uint8_t reg,std::uint32_t& value){std::uint8_t bytes[3];if(!static_cast<CheckedSensors*>(p)->read(reg,bytes,3))return false;
            value=std::uint32_t(bytes[0])<<16|std::uint32_t(bytes[1])<<8|bytes[2];return true;};
        io.battery=[](void*,float& value){const auto raw=::analogRead(17);if(raw<0||raw>1023)return false;
            value=static_cast<float>(static_cast<double>(raw)*0.00314);return true;};return io;
    }
private:
    std::uint8_t address_=42;
    bool read(std::uint8_t reg,std::uint8_t* out,std::uint8_t count){
        Wire.beginTransmission(address_);const bool copied=Wire.write(reg)==1;
        const auto status=Wire.endTransmission();if(!copied||status!=0)return false;
        if(Wire.requestFrom(address_,count)!=count)return false;
        for(unsigned i=0;i<count;++i){if(Wire.available()<1)return false;const int value=Wire.read();if(value<0)return false;out[i]=static_cast<std::uint8_t>(value);}
        return true;
    }
};
class Application final:public oem::LedEvents,public oem::ButtonEvents {
public:
    Application():storage_(),controller_(sensors_.binding(),storage_.binding(),clock()),
        engine_(controller_.services(),storage_.binding(),clock()),
        provision_(storage_.binding(),this,[](void* p){auto& self=*static_cast<Application*>(p);return !self.setup_complete_||self.controller_.idle();}),
        controls_(diagnostics_,hardware_,*this,*this),http_(){controller_.bind(engine_);}
    void begin(){
        Serial.begin(115200);Serial.println("PicoFerm standalone 1.2.5");
        // Read the packed OEM record without sanitize(), put(), commit() or
        // boot-time migration. Modified records live only in /pb/ SPIFFS.
        oem::PersistentData original;original.bytes.fill(0xff);EEPROM.begin(oem::PersistentData::size);
        if(EEPROM.length()>=oem::PersistentData::size)for(std::size_t i=0;i<original.bytes.size();++i)original.bytes[i]=EEPROM.read(i);
        controls_.initialize(original.bytes[0x63]);
        const bool mounted=storage_.begin();if(!mounted)Serial.println("ERROR: standalone filesystem is not mounted; no formatting was attempted");
        report(provision_.load());
        if(original.word(original.cookie_position)==0xdeadbeefu&&original.word(original.version_position)==1){
            api::NetworkCredentials imported;std::memcpy(imported.ssid,original.bytes.data(),32);std::memcpy(imported.password,original.bytes.data()+32,64);
            if(imported.ssid[0]){const auto result=provision_.import_network(imported);if(result.status!=409)report(result);}
        }
        std::uint8_t mac[6];WiFi.macAddress(mac);char uid[33];::snprintf(uid,sizeof(uid),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
        controller_.identity(uid);report(controller_.begin(original.bytes.data(),original.bytes.size()));
        engine_.set_telemetry_io(telemetry_.binding());report(engine_.begin());
        pico_http_server_initialize(&http_,&sdk::HttpListener::io,&listener_,api::Engine::http_handler,&engine_,pico_common_web_assets,pico_common_web_assets_count);
        pico_http_server_enable_pairing(&http_, false);
        pico_http_server_set_asset_io(&http_,&pico_common_web_asset_io,nullptr);
        apply_key();WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setAutoConnect(false);WiFi.setAutoReconnect(true);
        WiFi.hostname("picoferm");apply_network();
        provision_.take_key_changed();provision_.take_network_changed();
        setup_complete_=true;Serial.println("UART: status | key <64 lowercase hex> | wifi <SSID><TAB><password>");
    }
    void poll(){
        controller_.poll();controls_.poll_button();controls_.poll_leds(controller_.animation());
        for(unsigned n=0;n<32&&Serial.available()>0;++n){const int value=Serial.read();if(value<0)break;
            api::Result result;if(provision_.feed(static_cast<char>(value),result))report(result);}
        if(provision_.take_key_changed()){pico_http_server_disconnect(&http_);apply_key();}
        if(provision_.take_network_changed()){pico_http_server_disconnect(&http_);apply_network();}
        const bool connected=WiFi.status()==WL_CONNECTED;
        if(connected&&!connected_){char address[48];const IPAddress ip=WiFi.localIP();::snprintf(address,sizeof(address),"Local browser: http://%u.%u.%u.%u",ip[0],ip[1],ip[2],ip[3]);Serial.println(address);}
        if(!connected&&connected_)pico_http_server_disconnect(&http_);
        connected_=connected;
        if(!listener_.listening()&&connected)listener_.begin(80);
        pico_http_server_poll(&http_,::millis());engine_.poll();::yield();
    }
    void animation_changed(oem::Animation) override{}
    void long_press() override{controller_.long_press();}
private:
    Hardware hardware_;Diagnostics diagnostics_;CheckedSensors sensors_;
    sdk::SpiffsStorage storage_;sdk::AsyncTelemetry telemetry_;sdk::HttpListener listener_;
    Controller controller_;api::Engine engine_;api::Provisioning provision_;
    LocalControls controls_;PicoHttpServer http_;
    bool setup_complete_=false,connected_=false;
    api::Clock clock(){api::Clock result;result.context=this;result.millis=[](void*)->std::uint32_t{return ::millis();};return result;}
    void report(const api::Result& result){if(result.message)Serial.println(result.message);else if(!result.ok())Serial.println(result.error?result.error:"ERROR: initialization failed");}
    void apply_key(){pico_http_server_set_key(&http_,provision_.key(),provision_.key_configured()?64:0);}
    void apply_network(){
        WiFi.disconnect();
        if(provision_.network_configured()){const auto& network=provision_.network();WiFi.begin(network.ssid,network.password);}
    }
};
Application& application(){static Application owner;return owner;}
}}}
void setup(){picoferm::standalone::application().begin();}
void loop(){picoferm::standalone::application().poll();}
