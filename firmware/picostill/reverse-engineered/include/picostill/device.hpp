#pragma once
#include "picostill/http_server.hpp"
#include "picostill/wifi.hpp"
namespace picostill { namespace oem {
struct DevicePorts {
    virtual ~DevicePorts() = default;
    virtual void credential_variable(const char* scope,const char* name,const char* value,bool mask) = 0;
    virtual void factory_reset() = 0;
    virtual int scan_count() = 0;
    virtual const char* scan_ssid(unsigned index) = 0;
};
class ApplicationDevice final:public HttpDevice {
public:
    ApplicationDevice(Application& app,Controller& controller,Hardware& hardware,Wifi& wifi,
                      WifiStatus& wifi_status,BackendStatus& backend,DevicePorts& ports,Diagnostics& diagnostics,const char* sensor_csv) noexcept
        :app_(app),controller_(controller),hardware_(hardware),wifi_(wifi),wifi_status_(wifi_status),backend_(backend),ports_(ports),diagnostics_(diagnostics),csv_(sensor_csv) {}
    const char* version() override{return app_.data.version.data();}
    const char* ssid() override{return app_.ssid.data();}
    bool has_ssid() override{return app_.ssid[0]!=0;}
    bool has_station_ip() override{return wifi_.station_ip_obtained;}
    std::uint32_t station_ip() override{return wifi_.station_ip;}
    bool needs_update() override{return backend_.has_update_address;}
    // The OEM getter inspects requested_program, even after controller stop.
    bool is_program_running() override{return app_.requested_program!=ProgramId::None;}
    bool wifi_connected() override{return wifi_status_.state==WifiState::Connected;}
    bool sensors_enabled() override{return hardware_.sensors_enabled;}
    bool fan_enabled() override{return hardware_.fans.power.enabled && hardware_.fans.first.enabled;}
    const char* program_name() override{return to_string(app_.requested_program);}
    const char* sensor_csv() override{return csv_;}
    const char* hardware_error() override{return to_string(controller_.error);}
    void configure(const char* ssid,const char* password) override;
    void connect_to_lan() override{app_.connect_requested=true;}
    void start_program(ProgramId program) override{app_.request_program(program);}
    void stop_program() override{app_.request_stop();}
    void set_fan(bool enabled) override{hardware_.fans.set_first(enabled);}
    void set_pump(bool enabled) override{hardware_.pump.set_pump(enabled);}
    void set_sensors(bool enabled) override{hardware_.enable_sensors(enabled);}
    void factory_result(FactoryResult& result) override;
    void factory_reset() override{ports_.factory_reset();}
    int scan_count() override{return ports_.scan_count();}
    const char* scan_ssid(unsigned index) override{return ports_.scan_ssid(index);}
private:
    Application& app_;Controller& controller_;Hardware& hardware_;Wifi& wifi_;WifiStatus& wifi_status_;BackendStatus& backend_;
    DevicePorts& ports_;Diagnostics& diagnostics_;const char* csv_;
};
}} // namespace picostill::oem
