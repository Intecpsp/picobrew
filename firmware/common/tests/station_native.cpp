#include "station.hpp"
#include <WiFi.h>
#include <esp_wifi.h>
#include <tcpip_adapter.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace sdk=picobrew::standalone::esp32;
static unsigned checks=0,fail_at=0;
static int failure=-77,disconnect_result=0,dhcp_result=0;
static std::vector<unsigned> calls;
static wifi_config_t captured{};
WiFiClass WiFi;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"station check line %u: %s\n",__LINE__,#x);std::abort();}}while(0)
static int call(unsigned n){calls.push_back(n);return fail_at==n?failure:0;}
void WiFiClass::persistent(bool value){CHECK(!value);call(1);}
bool WiFiClass::enableSTA(bool value){CHECK(value);return call(2)==0;}
int esp_wifi_set_storage(int value){CHECK(value==WIFI_STORAGE_RAM);return call(3);}
int esp_wifi_set_auto_connect(bool value){CHECK(!value);return call(4);}
int esp_wifi_disconnect(){const int e=call(5);return e?e:disconnect_result;}
int esp_wifi_set_config(int interface,const wifi_config_t* config){CHECK(interface==WIFI_IF_STA);captured=*config;return call(6);}
int tcpip_adapter_dhcpc_start(int interface){CHECK(interface==TCPIP_ADAPTER_IF_STA);const int e=call(7);return e?e:dhcp_result;}
int esp_wifi_connect(){return call(8);}
static void reset(){calls.clear();fail_at=0;disconnect_result=dhcp_result=0;captured=wifi_config_t{};}
int main(){
    const std::string name(32,'s'),key(64,'a');
    CHECK(sdk::connect_station(name.c_str(),key.c_str())==0);CHECK(calls==std::vector<unsigned>({1,2,3,4,5,6,7,8}));
    CHECK(!std::memcmp(captured.sta.ssid,name.data(),32));CHECK(!std::memcmp(captured.sta.password,key.data(),64));
    for(auto byte:captured.sta.reserved)CHECK(byte==0);
    for(const auto& invalid:std::vector<std::pair<std::string,std::string>>{{"",""},{std::string(33,'s'),""},{"ssid","short"},{"ssid",std::string(64,'z')},{"ssid",std::string(65,'a')},{"ssid","pass\nword"}}){
        reset();CHECK(sdk::connect_station(invalid.first.c_str(),invalid.second.c_str())==ESP_ERR_INVALID_ARG);CHECK(calls.empty());
    }
    for(unsigned n=2;n<=8;++n){reset();fail_at=n;CHECK(sdk::connect_station("ssid","password")== (n==2?ESP_FAIL:failure));CHECK(calls.back()==n);}
    reset();disconnect_result=ESP_ERR_WIFI_NOT_CONNECT;dhcp_result=ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STARTED;
    CHECK(sdk::connect_station("ssid",nullptr)==0);for(auto byte:captured.sta.password)CHECK(byte==0);
    std::printf("ESP32 station: %u checks passed; full-width SSID/PSK, RAM config, first-error propagation, no association wait\n",checks);
}
