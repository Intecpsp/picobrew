#pragma once
#include "picoferm/backend.hpp"
#include "picoferm/wifi.hpp"
#include <string>
namespace picoferm { namespace oem {
enum class State:std::uint32_t {Undefined=0,Booting=1,Listening=2,Running=3,Logging=4,Connecting=5,Syncing=6,APMode=7,Calibrating=8,Validating=9,Finishing=10,Sleeping=11};
const char* state_name(State state) noexcept;
struct SystemPorts {
    virtual ~SystemPorts()=default;
    virtual void serial_begin(unsigned baud,unsigned configuration,unsigned mode,unsigned tx_pin)=0;
    virtual void serial_line(const char* value)=0;
    virtual void mac_address(std::uint8_t* bytes)=0;
    virtual void storage_begin(std::size_t size)=0;
    virtual std::size_t storage_size()=0;
    virtual void storage_read(std::uint8_t* bytes,std::size_t size)=0;
    virtual void remove_os_wifi_data()=0;
    virtual void deep_sleep(std::uint64_t microseconds,unsigned radio_mode)=0;
    virtual void restart()=0;
    virtual void update_reboot(bool enabled)=0;
    virtual int update(const char* url,const char* current_version)=0;
    virtual std::string update_error()=0;
};
struct WebServices {
    virtual ~WebServices()=default;
    virtual void initialize_dns(unsigned ttl,unsigned reply_code)=0;
    virtual bool start_dns(unsigned port,const char* domain,std::uint32_t address)=0;
    virtual void stop_dns()=0;
    virtual void start_http()=0;
    virtual void set_enabled(bool enabled)=0;
    virtual void poll()=0;
};
class Application final:public AcquisitionEvents,public LedEvents,public ButtonEvents,public WifiEvents {
public:
    Application(Ports& ports,HardwarePorts& hardware,Runtime& runtime,Formatter& formatter,WifiPorts& network,
                OutboundClient& client,SystemPorts& system,WebServices& web,
                const char* version="0.2.7",const char* website="www.picobrew.com");
    std::uint32_t listening_timeout=10000,ap_timeout=300000,connection_timeout=60000,validation_timeout=60000,
        acquisition_timeout=5000,finishing_timeout=60000,sync_timeout=180000,sleep_seconds=300,changed_at=0,poll_max=0;
    State state=State::Sleeping;
    SessionState session_state=SessionState::Undefined;
    bool credentials_changed=false,button_requested=false,allow_listen_timeout=true,run_requested=false,
        restart_requested=false,calibrate_requested=false,server_enabled=false;
    float battery_threshold=0.7f;
    std::uint32_t storage_allocation=512;
    DeviceData data;
    Measurements measurements;
    Adc adc;
    Acquisition acquisition;
    Leds leds;
    Button button;
    Wifi wifi;
    Backend backend;
    std::array<char,128> console={{0}};
    std::uint32_t console_length=0;bool console_ready=false;
    void setup(); // 0x40206e80
    void begin(); // 0x40206ffc: UART + setup + reference metadata
    void loop(); // 0x40207fe8
    void transition(State next); // 0x40206bf8
    bool timed_out(std::uint32_t now,std::uint32_t duration); // 0x40204d78
    void poll(); // 0x40207ba8
    void info(); // 0x4020526c
    void load_storage(); // 0x40203b30 + owning record
    void report_raw_storage(); // 0x4020484c
    void persist(); // 0x402050b4
    void restart(); // 0x40203088
    void sleep(std::uint32_t seconds); // 0x402030e4
    void factory_reset(); // 0x4020385c
    CalibrationStatus calibration(); // 0x402039cc
    void calibrate(); // 0x40202fe4
    void configure(char* result,std::size_t capacity,const char* ssid,const char* password); // 0x402044e8
    bool update(); // 0x402057a8
    void handle_sync(); // 0x4020597c
    void dispatch_console(); // 0x402070f0
    void run_embedded_tests(); // 0x4020686c and recovered component vectors
    // Owned entry for a complete command already queued by a caller. The OEM
    // dispatcher is retained; no new UART receiver is inserted into its loop.
    bool queue_console(const char* command);
    void measurement(float temperature,std::uint32_t raw_pressure) override;
    void animation_changed(Animation animation) override;
    void long_press() override;
    void wifi_changed(WifiState previous,WifiState next) override;
private:
    Ports& ports_;HardwarePorts& hardware_;Runtime& runtime_;Formatter& formatter_;SystemPorts& system_;WebServices& web_;
    const char* ssid()const{return reinterpret_cast<const char*>(measurements.data.bytes.data());}
    const char* password()const{return reinterpret_cast<const char*>(measurements.data.bytes.data()+32);}
};
}}
