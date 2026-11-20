// Reuse the original controller-layout transport, not its behavior model.
#include "controller_bridge.cpp"
#include "picostill/application.hpp"
namespace {
struct Services final:ApplicationServices {
    void poll(ServicePoll service) override {const char* names[]={"update","console_receiver","wifi","backend","http","console_commands"};ports.events.push_back(std::string("service:")+names[static_cast<unsigned>(service)]);}
    void integer(const char* scope,const char* key,std::uint32_t n) override {ports.events.push_back(std::string("integer:")+scope+":"+key+":"+std::to_string(n));}
    void listen_http(unsigned port) override {ports.events.push_back("listen:"+std::to_string(port));}
    void queue_console(const char* command) override {ports.events.push_back(std::string("queue_console:")+command);}
    void save_credentials(const EepromData& data) override {ports.events.push_back("save_credentials:"+hex(&data,sizeof(data)));}
    void request_firmware_address() override {ports.events.push_back("request_firmware_address");}
    void start_firmware_update() override {ports.events.push_back("start_firmware_update");}
    void send_error(StillError error) override {ports.events.push_back(std::string("send_error:")+to_string(error));}
    void erase_nvs_after_update() override {ports.events.push_back("erase_nvs_after_update");}
    void restart() override {ports.events.push_back("restart");}
} services;
void load_leds(Leds& l,const std::uint8_t* b){
    const unsigned p=0x155c;for(unsigned i=0;i<3;++i){l.pins[i]=read(b,p+i,1);l.values[i]=read(b,p+3+i,1);}
    std::memcpy(l.program.data(),b+p+8,64);l.length=read(b,p+0x48);l.remaining=read(b,p+0x4c);l.repeat=static_cast<std::int32_t>(read(b,p+0x50));
    l.completed=read(b,p+0x54);l.position=read(b,p+0x58);l.deadline=read(b,p+0x5c);
}
void save_leds(const Leds& l,std::uint8_t* b){
    const unsigned p=0x155c;for(unsigned i=0;i<3;++i)write(b,p+3+i,l.values[i],1);
    std::memcpy(b+p+8,l.program.data(),64);write(b,p+0x48,l.length);write(b,p+0x4c,l.remaining);write(b,p+0x50,static_cast<std::uint32_t>(l.repeat));
    write(b,p+0x54,l.completed);write(b,p+0x58,l.position);write(b,p+0x5c,l.deadline);
}
void load_app(Application& a,WifiStatus& w,BackendStatus& backend,const std::uint8_t* b){
    a.reset_button_window=read(b,4);a.completion_timeout=read(b,8);a.state=static_cast<StillState>(read(b,12));a.changed_at=read(b,16);a.error=static_cast<StillError>(read(b,20));
    std::memcpy(a.ssid.data(),b+0x78,32);std::memcpy(a.password.data(),b+0x98,64);a.maximum_poll_time=read(b,0x104);std::memcpy(&a.stored,b+0x108,100);
    a.connect_requested=read(b,0x16c,1);a.credentials_changed=read(b,0x16d,1);a.button_long=read(b,0x170)!=0;a.button_count=read(b,0x174);a.button_pending=read(b,0x178,1);
    a.wifi_event=static_cast<EventType>(read(b,0x17c));a.wifi_error=static_cast<StillError>(read(b,0x180));a.wifi_pending=read(b,0x184,1);
    a.requested_program=static_cast<ProgramId>(read(b,0x188));a.program_pending=read(b,0x18c,1);a.stop_pending=read(b,0x18d,1);a.http_listening=read(b,0x13c0,1);
    a.button.pressed=read(b,0x1438,1);a.button.press_count=read(b,0x143c);a.button.state=static_cast<ButtonState>(read(b,0x1440));a.button.changed_at=read(b,0x1444);
    a.menu_count=read(b,0x1ef4);a.menu_index=read(b,0x1efc);a.updater_state=read(b,0x16e8);
    w.state=static_cast<WifiState>(read(b,0x15f8));w.requested=static_cast<WifiState>(read(b,0x1600));w.website_ip=read(b,0x1614);
    if(read(b,0x15f4))w.requested_ssid=a.ssid.data();
    if(read(b,0x15f0))w.requested_password=a.password.data();
    backend.host_ip=read(b,0x1008);backend.done=read(b,0x100c,1);backend.has_update_address=read(b,0x100e,1);
}
void save_app(const Application& a,const WifiStatus& w,const BackendStatus& backend,std::uint8_t* b){
    write(b,12,static_cast<unsigned>(a.state));write(b,16,a.changed_at);write(b,20,static_cast<unsigned>(a.error));write(b,0x104,a.maximum_poll_time);
    write(b,0x16c,a.connect_requested,1);write(b,0x16d,a.credentials_changed,1);write(b,0x170,a.button_long);write(b,0x174,a.button_count);write(b,0x178,a.button_pending,1);
    write(b,0x17c,static_cast<unsigned>(a.wifi_event));write(b,0x180,static_cast<unsigned>(a.wifi_error));write(b,0x184,a.wifi_pending,1);
    write(b,0x188,static_cast<unsigned>(a.requested_program));write(b,0x18c,a.program_pending,1);write(b,0x18d,a.stop_pending,1);write(b,0x13c0,a.http_listening,1);
    write(b,0x1438,a.button.pressed,1);write(b,0x143c,a.button.press_count);write(b,0x1440,static_cast<unsigned>(a.button.state));write(b,0x1444,a.button.changed_at);
    write(b,0x1efc,a.menu_index);write(b,0x1600,static_cast<unsigned>(w.requested));write(b,0x15f4,w.requested_ssid?0x3ff00078:0);write(b,0x15f0,w.requested_password?0x3ff00098:0);write(b,0x1008,backend.host_ip);
}
}
extern "C" {
void application_run(unsigned op,std::uint8_t* bytes,std::uint32_t arg,std::uint32_t arg2,std::uint32_t now,int high){
    ports.events.clear();ports.clock=now;ports.high=high;ports.pressure={64,128,0,0};ports.adc={8,0,8,0,8,0,8,0};diagnostics.assertion_count=0;
    Hardware hardware(ports,ports,diagnostics,ports);Controller controller(hardware,ports,diagnostics,ports);load(hardware,controller,bytes+0x1a0,0x40);
    Leds leds(ports,diagnostics);load_leds(leds,bytes);WifiStatus wifi;BackendStatus backend;Application app(controller,leds,ports,diagnostics,services,wifi,backend);load_app(app,wifi,backend,bytes);
    switch(op){case 0:app.queue_button(arg!=0,arg2);break;case 1:app.queue_wifi(static_cast<EventType>(arg),static_cast<StillError>(arg2));break;
    case 2:app.set_state(static_cast<StillState>(arg));break;case 3:app.handle_button();break;case 4:app.handle_wifi();break;
    case 5:app.select_menu(arg);break;case 6:app.request_program(static_cast<ProgramId>(arg));break;case 7:app.request_stop();break;case 8:app.poll();break;}
    save(hardware,controller,bytes+0x1a0,0x40);save_leds(leds,bytes);save_app(app,wifi,backend,bytes);
}
}
