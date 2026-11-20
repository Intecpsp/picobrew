#include "startup.hpp"
#include <Arduino.h>
#include <esp_log.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
namespace sdk=picobrew::standalone::esp32;
static std::vector<int> calls;
static int status=0;
static bool bt=false;
static unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value)){std::fprintf(stderr,"startup check line %u: %s\n",__LINE__,#value);std::abort();}}while(0)
bool setCpuFrequencyMhz(std::uint32_t frequency){CHECK(frequency==240);calls.push_back(1);return true;}
bool psramInit(){calls.push_back(2);return true;}
void esp_log_level_set(const char* tag,esp_log_level_t level){CHECK(!std::strcmp(tag,"*")&&level==3);calls.push_back(3);}
int nvs_flash_init(){calls.push_back(4);return status;}
bool btInUse(){calls.push_back(5);return bt;}
int esp_bt_controller_mem_release(int mode){CHECK(mode==3);calls.push_back(6);return 0;}
void init(){calls.push_back(7);}
void initVariant(){calls.push_back(8);}
int main(){
    CHECK(sdk::nvs_initialization_status()==-1);
    for(int error:{0,0x110d,0x110f,-1})for(bool used:{false,true}){
        calls.clear();status=error;bt=used;CHECK(sdk::initialize_arduino()==error);CHECK(sdk::nvs_initialization_status()==error);
        const std::vector<int> expected=used?std::vector<int>{1,2,3,4,5,7,8}:std::vector<int>{1,2,3,4,5,6,7,8};CHECK(calls==expected);
    }
    std::printf("ESP32 startup: %u checks passed; exact NVS status retained, one initialization, no erase/retry\n",checks);
}
