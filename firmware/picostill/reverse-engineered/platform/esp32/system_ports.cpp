#include "system_ports.hpp"
#include <nvs_flash.h>
#include <esp_system.h>
#include <cstring>
#include <cmath>
#include <stdio.h>
namespace picostill { namespace oem {
void ArduinoSystemPorts::inspect_partitions(){configured_=esp_ota_get_boot_partition();running_=esp_ota_get_running_partition();factory_=esp_partition_find_first(ESP_PARTITION_TYPE_APP,ESP_PARTITION_SUBTYPE_APP_FACTORY,nullptr);}
void ArduinoSystemPorts::partition(const char* name,const esp_partition_t* value){char text[96];if(value)snprintf(text,sizeof(text),"Type %d subtype %d (offset 0x%08x)",value->type,value->subtype,value->address);else snprintf(text,sizeof(text),"%s","NULL");hardware_.variable("SYS",name,text);}
void ArduinoSystemPorts::report_partitions(){partition("FACTORY",factory_);partition("RUNNING",running_);if(configured_!=running_){hardware_.variable("SYS","PART","Not running the configured partition");partition("CONFIGURED",configured_);}}
void ArduinoSystemPorts::initialize_hardware(Hardware& hardware){hardware_.initialize_hardware(hardware.fans,hardware.pump,hardware.sensors);}
void ArduinoSystemPorts::initialize_wifi(){wifi_.initialize();}
std::uint64_t ArduinoSystemPorts::efuse_mac(){return ESP.getEfuseMac();}
void ArduinoSystemPorts::credential_variable(const char* scope,const char* name,const char* value,bool mask){if(!mask){hardware_.variable(scope,name,value);return;}const std::string hidden(std::strlen(value),'*');hardware_.variable(scope,name,hidden.c_str());}
int ArduinoSystemPorts::serial_available(){return serial_.available();}
int ArduinoSystemPorts::serial_read(){return serial_.read();}
void ArduinoSystemPorts::erase_nvs(){nvs_flash_erase();}
void ArduinoSystemPorts::select_factory_partition(){const esp_err_t result=esp_ota_set_boot_partition(factory_);if(result!=ESP_OK)serial_.printf("Factory partition selection failed: %d\r\n",result);}
void ArduinoSystemPorts::deep_sleep(std::uint64_t microseconds){ESP.deepSleep(microseconds);}
void ArduinoSystemPorts::restart(){ESP.restart();}
int ArduinoSystemPorts::scan_count(){return WiFi.scanComplete();}
const char* ArduinoSystemPorts::scan_ssid(unsigned index){scanned_ssid_=WiFi.SSID(index);return scanned_ssid_.c_str();}
void ArduinoSystemPorts::integer(const char* scope,const char* name,std::uint32_t value){char text[16];snprintf(text,sizeof(text),"%u",value);hardware_.variable(scope,name,text);}
void ArduinoSystemPorts::floating(const char* scope,const char* name,float value){char text[96];format_decimal(value,4,2,text,sizeof(text));hardware_.variable(scope,name,text);}
void ArduinoSystemPorts::format_decimal(double value,int width,unsigned precision,char* destination,std::size_t capacity){
    if(!destination || !capacity)return;
    // SDK dtostrf has no capacity argument. Its normal sensor domain is small;
    // protect the owned buffer for corrupted/extreme inputs before delegating.
    if(precision>16 || width>64 || width< -64 || (!std::isnan(value) && !std::isinf(value) && std::fabs(value)>1e60)){snprintf(destination,capacity,"ovf");return;}
    char text[128];::dtostrf(value,static_cast<signed char>(width),static_cast<unsigned char>(precision),text);snprintf(destination,capacity,"%s",text);
}
void ArduinoSystemPorts::console(const char* message){serial_.println(message);}
}} // namespace picostill::oem
