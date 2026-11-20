#include "backend_bridge.cpp"
#include "picoferm/application.hpp"
namespace {
struct FakeWifi final:WifiPorts {
    const std::uint32_t* words;
    explicit FakeWifi(const std::uint32_t* w):words(w){}
    void persistent(bool value) override{ports.events.push_back("persistent:"+std::to_string(value));}
    void auto_connect(bool value) override{ports.events.push_back("auto_connect:"+std::to_string(value));}
    void mode(unsigned value) override{ports.events.push_back("mode:"+std::to_string(value));}
    std::uint32_t status() override{ports.events.push_back("status:"+std::to_string(words[10]));return words[10];}
    void disconnect_station(bool value) override{ports.events.push_back("disconnect_station:"+std::to_string(value));}
    void disconnect_access_point(bool value) override{ports.events.push_back("disconnect_ap:"+std::to_string(value));}
    void scan_delete() override{ports.events.push_back("scan_delete");}
    void scan(bool asynchronous,bool hidden) override{ports.events.push_back("scan:"+std::to_string(asynchronous)+":"+std::to_string(hidden));}
    int scan_complete() override{ports.events.push_back("scan_complete:"+std::to_string(static_cast<std::int32_t>(words[11])));return words[11];}
    void ap_config(std::uint32_t local,std::uint32_t gateway,std::uint32_t mask) override{ports.events.push_back("ap_config:"+std::to_string(local)+":"+std::to_string(gateway)+":"+std::to_string(mask));}
    bool ap_start(const char* ssid,const char* password,unsigned channel,bool hidden,unsigned clients) override{ports.events.push_back(std::string("ap_start:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(hidden)+":"+std::to_string(clients));return words[12];}
    std::uint32_t begin_station(const char* ssid,const char* password,unsigned channel,bool connect) override{ports.events.push_back(std::string("begin_station:")+ssid+":"+password+":"+std::to_string(channel)+":"+std::to_string(connect));return words[13];}
    int resolve(const char* hostname,std::uint32_t& address) override{ports.events.push_back(std::string("resolve:")+hostname);address=words[15];return words[14];}
    std::uint32_t local_ip() override{ports.events.push_back("local_ip:"+std::to_string(words[16]));return words[16];}
    std::uint32_t gateway_ip() override{ports.events.push_back("gateway_ip:"+std::to_string(words[17]));return words[17];}
};
struct FakeSystem final:SystemPorts {
    std::uint8_t* stored;std::size_t length,seen=0;int update_result;std::string error;bool reboot=true;
    FakeSystem(std::uint8_t* p,unsigned n,int result,const char* text):stored(p),length(n),update_result(result),error(text){}
    void sync(){for(;seen<ports.events.size();++seen){const auto& value=ports.events[seen];if(value.compare(0,8,"persist:")==0&&length>=236)for(unsigned i=0;i<236;++i)stored[i]=std::stoul(value.substr(8+2*i,2),nullptr,16);}}
    void serial_begin(unsigned baud,unsigned config,unsigned mode,unsigned pin) override{ports.events.push_back("serial_begin:"+std::to_string(baud)+":"+std::to_string(config)+":"+std::to_string(mode)+":"+std::to_string(pin));}
    void serial_line(const char* value) override{ports.events.push_back(std::string("serial:")+value);}
    void mac_address(std::uint8_t* bytes) override{ports.events.push_back("mac");for(unsigned i=0;i<6;++i)bytes[i]=i*17;}
    void storage_begin(std::size_t size) override{ports.events.push_back("storage_begin:"+std::to_string(size));}
    std::size_t storage_size() override{return length;}
    void storage_read(std::uint8_t* bytes,std::size_t size) override{sync();std::memcpy(bytes,stored,size);}
    void remove_os_wifi_data() override{ports.events.push_back("remove_os_wifi");}
    void deep_sleep(std::uint64_t time,unsigned mode) override{ports.events.push_back("sleep:"+std::to_string(time)+":"+std::to_string(mode));}
    void restart() override{ports.events.push_back("restart");}
    void update_reboot(bool value) override{reboot=value;}
    int update(const char* url,const char* version) override{ports.events.push_back(std::string("update:")+url+":"+version);return update_result;}
    std::string update_error() override{ports.events.push_back("update_error");return error;}
};
struct FakeWeb final:WebServices {
    bool dns;explicit FakeWeb(bool result):dns(result){}
    void initialize_dns(unsigned ttl,unsigned reply) override{ports.events.push_back("dns_ttl:"+std::to_string(ttl));ports.events.push_back("dns_reply:"+std::to_string(reply));}
    bool start_dns(unsigned port,const char* domain,std::uint32_t ip) override{ports.events.push_back(std::string("dns_start:")+std::to_string(port)+":"+domain+":"+std::to_string(ip));return dns;}
    void stop_dns() override{ports.events.push_back("dns_stop");}void start_http() override{ports.events.push_back("http_start");}void set_enabled(bool) override{}
    void poll() override{ports.events.push_back("web_poll");}
};
void load_application(Application& a,const std::uint8_t* p,const char* werror,const char* berror){
    a.listening_timeout=read_word(p,4);a.ap_timeout=read_word(p,8);a.connection_timeout=read_word(p,12);a.validation_timeout=read_word(p,16);a.acquisition_timeout=read_word(p,20);a.finishing_timeout=read_word(p,24);a.sync_timeout=read_word(p,28);a.sleep_seconds=read_word(p,32);
    a.state=static_cast<State>(read_word(p,0x24));a.changed_at=read_word(p,0x28);a.session_state=static_cast<SessionState>(read_word(p,0x2c));a.credentials_changed=p[0x30];a.button_requested=p[0x31];a.allow_listen_timeout=p[0x32];a.run_requested=p[0x33];a.restart_requested=p[0x34];a.calibrate_requested=p[0x35];
    std::memcpy(&a.data,p+0x38,112);a.poll_max=read_word(p,0xbc);std::memcpy(a.measurements.data.bytes.data(),p+0xc0,236);
    a.adc.address=p[0x1c4];a.adc.reference_voltage=number(read_word(p,0x1c8));a.acquisition.state=read_word(p,0x1cc);a.acquisition.deadline=read_word(p,0x1d0);a.acquisition.raw_temperature=read_word(p,0x1d4);a.acquisition.raw_pressure=read_word(p,0x1d8);a.acquisition.continuous=p[0x1e4];load_backend(a.backend,p+0x1e8,berror);
    a.battery_threshold=number(read_word(p,0x63c));a.button.pressed=p[0x64c];a.button.long_reported=p[0x64d];a.button.deadline=read_word(p,0x650);a.button.woke_board=p[0x654];
    std::memcpy(a.console.data(),p+0x658,128);a.console_length=read_word(p,0x6d8);a.console_ready=p[0x6dc];std::memcpy(a.leds.pins,p+0x6e0,3);
    a.leds.animation=static_cast<Animation>(read_word(p,0x6f0));a.leds.mode=read_word(p,0x6f4);a.leds.steps=read_word(p,0x6f8);a.leds.period=read_word(p,0x6fc);for(unsigned i=0;i<3;++i)a.leds.enabled[i]=p[0x700+i];a.leds.deadline=read_word(p,0x704);a.leds.toggle=read_word(p,0x708);a.leds.rotating_index=read_word(p,0x70c);a.storage_allocation=read_word(p,0x710);
    a.wifi.flush_timeout=read_word(p,0x714);a.wifi.internet_ready=p[0x728];a.wifi.begin_failed=p[0x729];a.wifi.begin_status=read_word(p,0x72c);a.wifi.state=static_cast<WifiState>(read_word(p,0x738));a.wifi.changed_at=read_word(p,0x73c);a.wifi.target=static_cast<WifiState>(read_word(p,0x740));a.wifi.error=werror;a.wifi.ap_ip=read_word(p,0x750);a.wifi.host_ip=read_word(p,0x758);a.wifi.ssid=reinterpret_cast<const char*>(a.measurements.data.bytes.data());a.wifi.password=reinterpret_cast<const char*>(a.measurements.data.bytes.data()+32);a.server_enabled=p[0x7d4];
}
void save_application(const Application& a,std::uint8_t* p){
    write_word(p,0x24,static_cast<unsigned>(a.state));write_word(p,0x28,a.changed_at);write_word(p,0x2c,static_cast<unsigned>(a.session_state));p[0x30]=a.credentials_changed;p[0x31]=a.button_requested;p[0x32]=a.allow_listen_timeout;p[0x33]=a.run_requested;p[0x34]=a.restart_requested;p[0x35]=a.calibrate_requested;
    std::memcpy(p+0x38,&a.data,112);write_word(p,0xbc,a.poll_max);std::memcpy(p+0xc0,a.measurements.data.bytes.data(),236);p[0x1c4]=a.adc.address;write_word(p,0x1c8,bits(a.adc.reference_voltage));write_word(p,0x1cc,a.acquisition.state);write_word(p,0x1d0,a.acquisition.deadline);write_word(p,0x1d4,a.acquisition.raw_temperature);write_word(p,0x1d8,a.acquisition.raw_pressure);p[0x1e4]=a.acquisition.continuous;save_backend(a.backend,p+0x1e8);
    p[0x64c]=a.button.pressed;p[0x64d]=a.button.long_reported;write_word(p,0x650,a.button.deadline);p[0x654]=a.button.woke_board;std::memcpy(p+0x658,a.console.data(),128);write_word(p,0x6d8,a.console_length);p[0x6dc]=a.console_ready;
    write_word(p,0x6f0,static_cast<unsigned>(a.leds.animation));write_word(p,0x6f4,a.leds.mode);write_word(p,0x6f8,a.leds.steps);write_word(p,0x6fc,a.leds.period);for(unsigned i=0;i<3;++i)p[0x700+i]=a.leds.enabled[i];write_word(p,0x704,a.leds.deadline);write_word(p,0x708,a.leds.toggle);write_word(p,0x70c,a.leds.rotating_index);
    p[0x728]=a.wifi.internet_ready;p[0x729]=a.wifi.begin_failed;write_word(p,0x72c,a.wifi.begin_status);write_word(p,0x738,static_cast<unsigned>(a.wifi.state));write_word(p,0x73c,a.wifi.changed_at);write_word(p,0x740,static_cast<unsigned>(a.wifi.target));write_word(p,0x744,0);write_word(p,0x750,a.wifi.ap_ip);write_word(p,0x758,a.wifi.host_ip);p[0x7d4]=a.server_enabled;
}
std::string app_label,app_wifi_error,app_backend_error,app_sent;unsigned app_clock,app_position,app_reboot;
}
extern "C" {
unsigned ferm_application_run(const char* operation,std::uint8_t* bytes,std::uint8_t* storage,const std::uint32_t* params,const std::uint32_t* wifi,const std::uint32_t* reads,unsigned count,const std::uint32_t* items,unsigned item_count,const std::uint32_t* connections,unsigned attempts,const char* werror,const char* berror,const char* uerror){
    FakeHardware hw;hw.now=params[0];hw.tick=params[1];hw.analog=params[2];hw.reads.assign(reads,reads+count);ports.gpio=params[3];FakeRuntime runtime(hw);FakeFormatter formatter;FakeWifi network(wifi);FakeOutbound client;client.items.assign(items,items+item_count);client.connections.assign(connections,connections+attempts);client.alive=params[10];FakeSystem system(storage,params[4],params[5],uerror);FakeWeb web(params[6]);
    Application app(ports,hw,runtime,formatter,network,client,system,web);load_application(app,bytes,werror,berror);ports.events.clear();system.seen=0;app_label.clear();unsigned result=0;const std::string op(operation);
    if(op=="transition")app.transition(static_cast<State>(params[7]));else if(op=="timeout")result=app.timed_out(hw.now,params[7]);else if(op=="info")app.info();else if(op=="restart")app.restart();else if(op=="sleep")app.sleep(params[7]);else if(op=="factory_reset")app.factory_reset();else if(op=="calibrate")app.calibrate();
    else if(op=="calibration"){auto value=app.calibration();result=value.error|(unsigned(value.can_calibrate)<<8);app_label=value.message;}
    else if(op=="raw_storage")app.report_raw_storage();else if(op=="load_storage")app.load_storage();else if(op=="persist")app.persist();else if(op=="led_callback")app.animation_changed(Animation::Off);else if(op=="button_callback")app.long_press();
    else if(op=="wifi_callback")app.wifi_changed(static_cast<WifiState>(params[7]),static_cast<WifiState>(params[8]));else if(op=="update")result=app.update();else if(op=="handle_sync")app.handle_sync();else if(op=="console")app.dispatch_console();
    else if(op=="poll")for(unsigned i=0;i<params[9];++i)app.poll();else if(op=="loop")app.loop();else if(op=="setup")app.setup();else if(op=="begin")app.begin();else if(op=="self_tests")app.run_embedded_tests();
    save_application(app,bytes);std::memset(bytes+0x1b4,0,16);system.sync();app_wifi_error=app.wifi.error;app_backend_error=app.backend.transport_error?app.backend.transport_error:"";app_sent=client.sent;app_clock=hw.now;app_position=client.position;app_reboot=system.reboot;return result;
}
const char* ferm_application_label(){return app_label.c_str();}const char* ferm_application_wifi_error(){return app_wifi_error.c_str();}const char* ferm_application_backend_error(){return app_backend_error.c_str();}const char* ferm_application_sent(){return app_sent.c_str();}
unsigned ferm_application_clock(){return app_clock;}unsigned ferm_application_position(){return app_position;}unsigned ferm_application_reboot(){return app_reboot;}
}
