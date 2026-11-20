#pragma once
#include "picoferm/wifi.hpp"
namespace picoferm { namespace oem {
class ArduinoWifiPorts final:public WifiPorts {
public:
    void persistent(bool enabled) override;void auto_connect(bool enabled) override;void mode(unsigned mode) override;
    std::uint32_t status() override;void disconnect_station(bool power_off) override;void disconnect_access_point(bool power_off) override;
    void scan_delete() override;void scan(bool asynchronous,bool hidden) override;int scan_complete() override;
    void ap_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t mask) override;
    bool ap_start(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) override;
    std::uint32_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) override;
    int resolve(const char* hostname,std::uint32_t& address) override;std::uint32_t local_ip() override;std::uint32_t gateway_ip() override;
};
}}
