// Complete application composition; OS/SDK ownership is supplied by ESP32 ports.
#pragma once
#include "picostill/backend.hpp"
#include "picostill/device.hpp"
#include "picostill/reports.hpp"
#include "picostill/storage.hpp"
#include "picostill/update.hpp"
namespace picostill { namespace oem {
struct SystemPorts {
    virtual ~SystemPorts() = default;
    virtual void inspect_partitions() = 0;
    virtual void report_partitions() = 0;
    virtual void initialize_hardware(Hardware& hardware) = 0;
    virtual void initialize_wifi() = 0;
    virtual std::uint64_t efuse_mac() = 0;
    virtual void credential_variable(const char* scope,const char* name,const char* value,bool mask) = 0;
    virtual int serial_available() = 0;
    virtual int serial_read() = 0;
    virtual void erase_nvs() = 0;
    virtual void select_factory_partition() = 0;
    virtual void deep_sleep(std::uint64_t microseconds) = 0;
    virtual void restart() = 0;
    virtual int scan_count() = 0;
    virtual const char* scan_ssid(unsigned index) = 0;
};
class Machine final:public ApplicationServices,public DevicePorts {
public:
    Machine(HardwarePorts& hardware_ports,I2cPorts& i2c,DiagnosticPorts& diagnostic_ports,StoragePorts& storage_ports,
            WifiPorts& wifi_ports,HttpRuntime& runtime,HttpListener& listener,OutboundClient& backend_client,
            OutboundClient& update_client,UpdatePorts& update_ports,ReportPorts& report_ports,SystemPorts& system) noexcept;
    Diagnostics diagnostics;
    WifiStatus wifi_status;
    BackendStatus backend_status;
    ApplicationReports reports;
    Hardware hardware;
    Controller controller;
    Leds leds;
    Application application;
    CredentialStorage storage;
    ApplicationWifiEvents wifi_events;
    Wifi wifi;
    Backend backend;
    FirmwareUpdate updater;
    UpdateParameters update_parameters;
    ApplicationDevice device;
    HttpServer http;
    std::array<char,128> receiving={{0}},queued={{0}};
    std::uint32_t receiving_length=0,receiving_remaining=127,queued_length=0,queued_remaining=127;
    void setup();
    void poll(){application.poll();}
    void poll(ServicePoll service) override;
    void integer(const char* scope,const char* name,std::uint32_t value) override;
    void listen_http(unsigned port) override;
    void queue_console(const char* command) override;
    void save_credentials(const EepromData& data) override{storage.save(data);}
    void request_firmware_address() override{backend.request_firmware();}
    void start_firmware_update() override;
    void send_error(StillError error) override{backend.send_error(error);}
    void erase_nvs_after_update() override{system_.erase_nvs();}
    void restart() override{system_.restart();}
    void credential_variable(const char* scope,const char* name,const char* value,bool mask) override{system_.credential_variable(scope,name,value,mask);}
    void factory_reset() override;
    int scan_count() override{return system_.scan_count();}
    const char* scan_ssid(unsigned index) override{return system_.scan_ssid(index);}
    void receive_console();
    void dispatch_console();
    void report_info();
    void report_eeprom();
    void sleep(unsigned seconds);
    void test_leds();
    void self_tests();
private:
    StoragePorts& storage_ports_;HardwarePorts& hardware_ports_;HttpRuntime& runtime_;HttpListener& listener_;ReportPorts& report_ports_;SystemPorts& system_;
    void append_command(const char* command);
};
}} // namespace picostill::oem
