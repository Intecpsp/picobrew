#pragma once
#include "picostill/machine.hpp"
#include "arduino_ports.hpp"
#include "wifi_ports.hpp"
#include <esp_ota_ops.h>
namespace picostill { namespace oem {
class ArduinoSystemPorts final:public SystemPorts,public ReportPorts {
public:
    ArduinoSystemPorts(ArduinoPorts& hardware,ArduinoWifiPorts& wifi,HardwareSerial& serial=Serial) noexcept:hardware_(hardware),wifi_(wifi),serial_(serial) {}
    void inspect_partitions() override;
    void report_partitions() override;
    void initialize_hardware(Hardware& hardware) override;
    void initialize_wifi() override;
    std::uint64_t efuse_mac() override;
    void credential_variable(const char* scope,const char* name,const char* value,bool mask) override;
    int serial_available() override;
    int serial_read() override;
    void erase_nvs() override;
    void select_factory_partition() override;
    void deep_sleep(std::uint64_t microseconds) override;
    void restart() override;
    int scan_count() override;
    const char* scan_ssid(unsigned index) override;
    void integer(const char* scope,const char* name,std::uint32_t value) override;
    void floating(const char* scope,const char* name,float value) override;
    void format_decimal(double value,int width,unsigned precision,char* destination,std::size_t capacity) override;
    void console(const char* message) override;
private:
    ArduinoPorts& hardware_;ArduinoWifiPorts& wifi_;HardwareSerial& serial_;
    const esp_partition_t* configured_=nullptr;const esp_partition_t* running_=nullptr;const esp_partition_t* factory_=nullptr;
    String scanned_ssid_;
    void partition(const char* name,const esp_partition_t* value);
};
}} // namespace picostill::oem
