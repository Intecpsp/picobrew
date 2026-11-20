#pragma once
#include "picostill/wifi.hpp"
#include <WiFi.h>

namespace picostill { namespace oem {
class ArduinoWifiPorts final:public WifiPorts {
public:
    explicit ArduinoWifiPorts(HardwareSerial& serial=Serial) noexcept:serial_(serial) {}
    void initialize();
    unsigned station_status() override;
    void mode(unsigned value) override;
    void scan(bool asynchronous,bool hidden,bool passive,unsigned maximum_ms) override;
    int scan_complete() override;
    void enable_access_point(bool enabled) override;
    void access_point_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t subnet) override;
    bool start_access_point(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) override;
    int wait_status_bits(unsigned bits,unsigned timeout) override;
    std::uint32_t access_point_ip() override;
    bool disconnect_access_point(bool power_off) override;
    std::uint8_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) override;
    void hostname(const char* value) override;
    std::uint32_t local_ip() override;
    std::uint32_t gateway_ip() override;
    int resolve(const char* host,std::uint32_t& address) override;
    bool disconnect_station(bool power_off,bool erase_config) override;
    void integer(const char* scope,const char* name,std::uint32_t value) override;
    void delay_ms(std::uint32_t duration) override;
private:
    HardwareSerial& serial_;
};
}} // namespace picostill::oem
