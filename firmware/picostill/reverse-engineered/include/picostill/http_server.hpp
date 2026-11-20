#pragma once
#include "picostill/http.hpp"
#include "picostill/controller.hpp"

namespace picostill { namespace oem {
struct FactoryResult {
    FullTestResult test;
    std::array<float,200> pressure = {{0}};
    std::uint32_t pressure_count = 0;
};
struct HttpDevice {
    virtual ~HttpDevice() = default;
    virtual const char* version() = 0;
    virtual const char* ssid() = 0;
    virtual bool has_ssid() = 0;
    virtual bool has_station_ip() = 0;
    virtual std::uint32_t station_ip() = 0;
    virtual bool needs_update() = 0;
    virtual bool is_program_running() = 0;
    virtual bool wifi_connected() = 0;
    virtual bool sensors_enabled() = 0;
    virtual bool fan_enabled() = 0;
    virtual const char* program_name() = 0;
    virtual const char* sensor_csv() = 0;
    virtual const char* hardware_error() = 0;
    virtual void configure(const char* ssid,const char* password) = 0;
    virtual void connect_to_lan() = 0;
    virtual void start_program(ProgramId program) = 0;
    virtual void stop_program() = 0;
    virtual void set_fan(bool enabled) = 0;
    virtual void set_pump(bool enabled) = 0;
    virtual void set_sensors(bool enabled) = 0;
    virtual void factory_result(FactoryResult& result) = 0;
    virtual void factory_reset() = 0;
    virtual int scan_count() = 0;
    virtual const char* scan_ssid(unsigned index) = 0;
};
class HttpServer {
public:
    HttpServer(HttpListener& listener,HttpRuntime& runtime,Diagnostics& diagnostics,HttpDevice& device) noexcept
        :listener_(listener),runtime_(runtime),diagnostics_(diagnostics),device_(device) {}
    std::uint32_t timeout = 5000;
    HttpRequest request;
    WifiForm form;
    void poll();
    // Returns whether the original handler explicitly flushes this client.
    bool dispatch(HttpStream& stream);
    void write_state(HttpStream& stream);
    void write_factory_result(HttpStream& stream,const FactoryResult& result);
    void write_page(HttpStream& stream,bool wifi);
    void write_browser_state(HttpStream& stream);
    void write_scan_options(HttpStream& stream);
private:
    HttpListener& listener_;
    HttpRuntime& runtime_;
    Diagnostics& diagnostics_;
    HttpDevice& device_;
};
}} // namespace picostill::oem
