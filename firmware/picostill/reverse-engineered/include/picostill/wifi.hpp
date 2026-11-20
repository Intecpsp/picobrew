#pragma once
#include "picostill/application.hpp"

namespace picostill { namespace oem {
const char* wifi_status_label(std::uint8_t status) noexcept;
struct WifiPorts {
    virtual ~WifiPorts() = default;
    virtual unsigned station_status() = 0;
    virtual void mode(unsigned value) = 0;
    virtual void scan(bool asynchronous,bool hidden,bool passive,unsigned maximum_ms) = 0;
    virtual int scan_complete() = 0;
    virtual void enable_access_point(bool enabled) = 0;
    virtual void access_point_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t subnet) = 0;
    virtual bool start_access_point(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) = 0;
    virtual int wait_status_bits(unsigned bits,unsigned timeout) = 0;
    virtual std::uint32_t access_point_ip() = 0;
    virtual bool disconnect_access_point(bool power_off) = 0;
    virtual std::uint8_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) = 0;
    virtual void hostname(const char* value) = 0;
    virtual std::uint32_t local_ip() = 0;
    virtual std::uint32_t gateway_ip() = 0;
    virtual int resolve(const char* host,std::uint32_t& address) = 0;
    virtual bool disconnect_station(bool power_off,bool erase_config) = 0;
    virtual void integer(const char* scope,const char* name,std::uint32_t value) = 0;
    virtual void delay_ms(std::uint32_t duration) = 0;
};
struct WifiEvents {
    virtual ~WifiEvents() = default;
    virtual void wifi_event(EventType type,StillError error) = 0;
};
class Wifi final {
public:
    Wifi(WifiStatus& status,WifiPorts& network,HardwarePorts& hardware,Diagnostics& diagnostics,WifiEvents& events) noexcept
        : status_(status),network_(network),hardware_(hardware),diagnostics_(diagnostics),events_(events) {}
    std::uint32_t power_delay=500,disconnect_delay=1000,connection_timeout=10000,changed_at=0;
    const char* ap_ssid=nullptr;
    const char* website="www.picobrew.com";
    std::uint32_t ap_ip=0x0104a8c0,station_ip=0;
    bool internet_ready=false,station_ip_obtained=false;
    void set_state(WifiState next);
    void fail(StillError error);
    void poll();
private:
    WifiStatus& status_;
    WifiPorts& network_;
    HardwarePorts& hardware_;
    Diagnostics& diagnostics_;
    WifiEvents& events_;
    void ip_variable(const char* name,std::uint32_t address);
};
class ApplicationWifiEvents final:public WifiEvents {
public:
    explicit ApplicationWifiEvents(Application& application) noexcept:application_(application) {}
    void wifi_event(EventType type,StillError error) override {application_.queue_wifi(type,error);}
private:
    Application& application_;
};
}} // namespace picostill::oem
