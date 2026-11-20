#pragma once
#include "picoferm/hardware.hpp"
namespace picoferm { namespace oem {
enum class WifiState:std::uint32_t {Booting=0,Idle=1,Scanning=2,AccessPoint=3,Connecting=4,Checking=5,Connected=6,FlushingAPClient=7};
const char* wifi_state_name(WifiState state) noexcept;
const char* wifi_status_name(std::uint32_t status) noexcept;
struct WifiPorts {
    virtual ~WifiPorts()=default;
    virtual void persistent(bool enabled)=0;
    virtual void auto_connect(bool enabled)=0;
    virtual void mode(unsigned mode)=0;
    virtual std::uint32_t status()=0;
    virtual void disconnect_station(bool power_off)=0;
    virtual void disconnect_access_point(bool power_off)=0;
    virtual void scan_delete()=0;
    virtual void scan(bool asynchronous,bool hidden)=0;
    virtual int scan_complete()=0;
    virtual void ap_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t mask)=0;
    virtual bool ap_start(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients)=0;
    virtual std::uint32_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect)=0;
    virtual int resolve(const char* hostname,std::uint32_t& address)=0;
    virtual std::uint32_t local_ip()=0;
    virtual std::uint32_t gateway_ip()=0;
};
struct WifiEvents {virtual ~WifiEvents()=default;virtual void wifi_changed(WifiState previous,WifiState next)=0;};
class Wifi {
public:
    Wifi(Ports& diagnostics,HardwarePorts& hardware,WifiPorts& network,WifiEvents& events) noexcept
        : diagnostics_(diagnostics),hardware_(hardware),network_(network),events_(events) {}
    std::uint32_t flush_timeout=2000,begin_status=0,changed_at=0;
    const char* ap_ssid="";const char* ssid="";const char* password="";const char* website="www.picobrew.com";
    WifiState state=WifiState::Booting,target=WifiState::Idle;
    bool internet_ready=false,begin_failed=false;
    const char* error="";
    std::uint32_t ap_ip=0x0104a8c0,host_ip=0;
    void initialize(); // 0x40205558
    void request(WifiState next); // 0x40204ad0
    void connect(const char* name,const char* pass); // 0x402055e4
    void set_state(WifiState next); // 0x40205b5c
    void fail(std::uint32_t status); // 0x40205d78
    void poll(); // 0x40205e08
private:
    Ports& diagnostics_;HardwarePorts& hardware_;WifiPorts& network_;WifiEvents& events_;
    void ip_text(const char* key,std::uint32_t value);
};
}}
