/* Production bridge and connection loop with scheduled driver events.
 * The real standalone-patched SDK callbacks consume every event below. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <string>
#include <stdlib.h>
#include "platform/energia_network.cpp"
#include "pico_c/local_history.h"

bool WiFiClass::_initialized = true, WiFiClass::_connecting = false;
volatile uint32_t WiFiClass::local_IP;
volatile wl_status_t WiFiClass::WiFi_status = WL_DISCONNECTED;
char WiFiClass::connected_ssid[MAX_SSID_LEN];
unsigned char WiFiClass::connected_bssid[BSSID_LEN];
unsigned WiFiClass::_connectedDeviceCount;
WiFiClass WiFi;
SerialClass Serial;
FlashClass SerFlash;
static void unexpected() __attribute__((noreturn));
static void unexpected() {fputs("Unexpected unrelated driver operation\n",stderr);abort();}
/* COFF linkers may retain these unrelated local-control sections even when
 * ELF/Mach-O discard them. Supply checked failure boundaries so this fixture
 * has complete linkage without ever simulating a brew as a successful no-op. */
extern "C" void pico_history_finish(bool) {unexpected();}
extern "C" void pico_actuators_all_off(PicoActuators*) {unexpected();}
extern "C" bool pico_control_stop(PicoControl*) {unexpected();}
extern "C" void pico_recipe_run_brew_program(PicoRecipeRuntime*,PicoRecipe*) {unexpected();}
extern "C" void pico_recipe_program_initialize(PicoRecipeRuntime*,PicoProgramContext*,uint32_t,uint32_t) {unexpected();}
extern "C" int pico_api_fetch_recipe(PicoNetwork*,const char*,int,int) {unexpected();}
extern "C" bool pico_recipe_session_start(PicoRecipeRuntime*,bool) {unexpected();}
extern "C" bool pico_recipe_run(PicoRecipeRuntime*,PicoRecipe*,bool) {unexpected();}
extern "C" void pico_recipe_run_cooking_program(PicoRecipeRuntime*,uint32_t) {unexpected();}
extern "C" void pico_recipe_manual_program(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_rinse(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_first_rinse(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_descale(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_circulate(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_drain_reservoir(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_drain_filter(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_recipe_rack_beer(PicoRecipeRuntime*) {unexpected();}
extern "C" void pico_utilities_menu() {unexpected();}
extern "C" void pico_settings_menu() {unexpected();}
extern "C" bool pico_storage_load_wifi(PicoNetwork*) {unexpected();}
void SerialClass::print(const char*) {unexpected();}
void SerialClass::print(float,unsigned) {unexpected();}
void FlashClass::begin() {unexpected();}
int32_t FlashClass::open(const char*,int32_t) {unexpected();}
int FlashClass::close() {unexpected();}
int FlashClass::read() {unexpected();}
int32_t FlashClass::write(const uint8_t*,size_t) {unexpected();}
int FlashClass::del(const char*) {unexpected();}
int WiFiClass::scanNetworks() {unexpected();}
const char* WiFiClass::SSID(uint8_t) {unexpected();}
uint8_t WiFiClass::encryptionType(uint8_t) {unexpected();}
int32_t WiFiClass::RSSI(uint8_t) {unexpected();}
void WiFiClass::macAddress(uint8_t*) {unexpected();}
extern "C" uint32_t SHAMD5IntStatus(uint32_t,bool) {unexpected();}
extern "C" void SHAMD5IntDisable(uint32_t,uint32_t) {unexpected();}
extern "C" void SHAMD5IntEnable(uint32_t,uint32_t) {unexpected();}
extern "C" void SHAMD5IntRegister(uint32_t,void(*)(void)) {unexpected();}
extern "C" void SHAMD5ConfigSet(uint32_t,uint32_t) {unexpected();}
extern "C" bool SHAMD5DataProcess(uint32_t,uint8_t*,unsigned,uint8_t*) {unexpected();}
extern "C" void PRCMPeripheralClkEnable(uint32_t,uint32_t) {unexpected();}
extern "C" void PRCMMCUReset(bool) {unexpected();}
extern "C" int sl_FsOpen(const _u8*,_u32,_u32*,_i32*) {unexpected();}
extern "C" int sl_FsRead(_i32,_u32,_u8*,_u32) {unexpected();}
extern "C" int sl_FsWrite(_i32,_u32,_u8*,_u32) {unexpected();}
extern "C" int sl_FsClose(_i32,const _u8*,const _u8*,_u32) {unexpected();}
extern "C" int sl_NetAppPingStart(const SlPingStartCommand_t*,int,SlPingReport_t*,void*) {unexpected();}
void WiFiClass::_registerNewDeviceIP(uint8_t*,uint8_t*) {}
void WiFiClass::_unregisterDevice(uint8_t*) {}

static const uint32_t old_address = 0xc0a80132, new_address = 0x0a00018f;
struct Event { uint32_t due; int kind; uint32_t address; };
static std::vector<Event> pending;
static std::vector<std::string> calls;
static uint32_t now, cancelled_at;
static unsigned joins, offline_polls, dhcp_requests, profile_deletes, network_changes;
static int disconnect_error, dhcp_error, get_error, policy_error, join_error, profile_error;
static bool disconnect_timeout, immediate_disconnect, immediate_association, immediate_lease, no_association, no_lease;
static bool old_socket_open, require_offline_before_join;
static uint32_t config_address;
static PicoApplication service_application;
static PicoApplicationHardware service_hardware;
static void wlan(int event) {
    SlWlanEvent_t value = {};
    value.Event = event;
    memcpy(value.EventData.STAandP2PModeWlanConnected.ssid_name,"new-network",11);
    value.EventData.STAandP2PModeWlanConnected.ssid_len = 11;
    SimpleLinkWlanEventHandler(&value);
}
static void ip_event(uint32_t address, int type=SL_NETAPP_IPV4_IPACQUIRED_EVENT) {
    SlNetAppEvent_t value = {};
    value.Event = type; value.EventData.ipAcquiredV4.ip = address;
    SimpleLinkNetAppEventHandler(&value);
}
static void service_events() {
    for (size_t i=0;i<pending.size();) {
        if (pending[i].due > now) {++i;continue;}
        Event event=pending[i]; pending.erase(pending.begin()+i);
        if (event.kind==3) {config_address=event.address;ip_event(event.address);}
        else wlan(event.kind);
    }
}
bool WiFiClass::init() { _initialized=true; return true; }
uint8_t WiFiClass::status() { service_events();return WiFi_status; }
uint32_t millis() {return now;}
void delay(uint32_t ms) {
    now+=ms;
    if (cancelled_at && now>=cancelled_at) pico_local_control_trip_filter();
}
extern "C" void pico_history_poll() {}
extern "C" void pico_telemetry_poll() {}
extern "C" void pico_telemetry_network_changed() {
    ++network_changes;calls.push_back("telemetry-network-change");
}
static void socket_service(void*) {
    if (WiFiClass::WiFi_status!=WL_CONNECTED) {++offline_polls;old_socket_open=false;}
}
extern "C" uint32_t sl_Htonl(uint32_t x) {return __builtin_bswap32(x);}
extern "C" int sl_WlanPolicySet(int type,int policy,const void*,int) {
    if (type==SL_POLICY_SCAN) return 0;
    assert(type==SL_POLICY_CONNECTION); calls.push_back("policy:"+std::to_string(policy));
    return policy_error;
}
static std::vector<Sl_WlanNetworkEntry_t> radio;
static unsigned scan_reads;
extern "C" int sl_WlanGetNetworkList(unsigned char start, unsigned char count, Sl_WlanNetworkEntry_t *out) {
    assert(!start && count==20 && radio.size()<=count);
    ++scan_reads;
    for (size_t i=0;i<radio.size();++i) out[i]=radio[i];
    return static_cast<int>(radio.size());
}
static Sl_WlanNetworkEntry_t ap(const std::string& name, uint8_t mode, int8_t signal) {
    Sl_WlanNetworkEntry_t row={};
    assert(name.size()<=sizeof row.ssid);
    memcpy(row.ssid,name.data(),name.size());
    row.ssid_len=name.size(); row.sec_type=mode; row.rssi=signal;
    return row;
}
extern "C" int sl_WlanDisconnect() {
    calls.push_back("disconnect");
    if(disconnect_error)return disconnect_error;
    if(immediate_disconnect)wlan(SL_WLAN_DISCONNECT_EVENT);
    else if(!disconnect_timeout) pending.push_back({now+20,SL_WLAN_DISCONNECT_EVENT,0});
    return 0;
}
extern "C" int sl_WlanProfileDel(int index) {
    assert(index==255);++profile_deletes;calls.push_back("delete-profiles");return 0;
}
extern "C" int sl_NetCfgSet(int id,int option,int length,const unsigned char* value) {
    assert(id==SL_IPV4_STA_P2P_CL_DHCP_ENABLE && option==1 && length==1 && *value==1);
    assert(WiFiClass::local_IP==0 && !WiFiClass::_connecting);
    ++dhcp_requests;calls.push_back("dhcp");return dhcp_error;
}
extern "C" int sl_NetCfgGet(int id,void*,unsigned char* length,unsigned char* bytes) {
    assert(id==SL_IPV4_STA_P2P_CL_GET_INFO && *length==sizeof(SlNetCfgIpV4Args_t));
    SlNetCfgIpV4Args_t value={config_address,0xffffff00,0x0a000101,0x01010101};
    memcpy(bytes,&value,sizeof(value));return get_error;
}
static unsigned expected_security;
static std::string expected_key;
static void verify_station_key(const SlSecParams_t *parameters) {
    assert(parameters->Type == expected_security);
    assert(parameters->KeyLen == expected_key.size());
    assert(!memcmp(parameters->Key, expected_key.data(), parameters->KeyLen));
}
extern "C" int sl_WlanConnect(const signed char* name,int size,const void*,const SlSecParams_t* security,const void*) {
    assert(std::string(reinterpret_cast<const char*>(name),size)=="new-network");
    verify_station_key(security);
    assert(WiFiClass::_connecting && WiFiClass::local_IP==0);
    if(require_offline_before_join) assert(!old_socket_open && offline_polls>0);
    ++joins;calls.push_back("join");
    if(join_error)return join_error;
    if(!no_association) {
        if(immediate_association)wlan(SL_WLAN_CONNECT_EVENT);
        else pending.push_back({now+20,SL_WLAN_CONNECT_EVENT,0});
        if(!no_lease) {
            if(immediate_lease){config_address=new_address;ip_event(new_address);}
            else pending.push_back({now+250,3,new_address});
        }
    }
    return 0;
}
extern "C" int sl_WlanProfileAdd(const signed char* name,int size,const void*,const SlSecParams_t* security,const void*,int,int) {
    verify_station_key(security);
    assert(std::string(reinterpret_cast<const char*>(name),size)=="new-network");
    calls.push_back("save-profile");return profile_error;
}
static void silent(void*,const char*) {}
static PicoNetworkPlatform test_platform;
static PicoNetwork network;
static PicoSettings settings;
static void setup() {
    pending.clear();calls.clear();now=cancelled_at=0;
    joins=offline_polls=dhcp_requests=profile_deletes=network_changes=0;
    disconnect_error=dhcp_error=get_error=policy_error=join_error=profile_error=0;
    disconnect_timeout=immediate_disconnect=immediate_association=immediate_lease=no_association=no_lease=false;
    old_socket_open=true;require_offline_before_join=false;
    connection_failed=restore_connection_policy=false;
    WiFiClass::_initialized=true;WiFiClass::_connecting=true;
    WiFiClass::WiFi_status=WL_CONNECTED;WiFiClass::local_IP=old_address;
    memcpy(WiFiClass::connected_ssid,"old-network",12);
    config_address=old_address;
    service_application={};service_hardware={};service_hardware.millis=clock_ms;
    service_application.hardware=&service_hardware;
    pico_local_control_bind(&service_application,socket_service,NULL);
    pico_local_control_ready(true);
    test_platform={};settings={};network={};
    strcpy(settings.wifi_ssid,"new-network");strcpy(settings.wifi_password,"private-password");
    settings.wifi_security=2;
    expected_security=SL_SEC_TYPE_WPA; expected_key=settings.wifi_password;
    test_platform.millis=clock_ms;test_platform.delay_ms=delay_ms;test_platform.serial_write=silent;
    test_platform.wifi_begin=begin;test_platform.wifi_status=status;
    test_platform.wifi_connecting=connecting;test_platform.wifi_disconnect=disconnect;
    test_platform.local_ip=local_ip;
    network.settings=&settings;network.platform=&test_platform;
}
static void expect_no_addresses() {
    PicoIpv4 ip={{1,2,3,4}},dns=ip,gate=ip,mask=ip;
    ip_configuration(NULL,&ip,&dns,&gate,&mask);
    assert(pico_network_ipv4_is_zero(ip) && pico_network_ipv4_is_zero(dns) &&
           pico_network_ipv4_is_zero(gate) && pico_network_ipv4_is_zero(mask));
}
int main() {
    setup();
    const std::string full(32,'x');
    radio={ap("Mesh",0,-10),ap(full,2,-40),ap("Mesh",2,-75),ap("mesh",2,-55),
           ap("Mesh",2,-30),ap("",0,-10),ap(std::string("x\0y",3),2,-50)};
    PicoWifiNetwork rows[PICO_WIFI_SCAN_LIMIT];
    assert(scan_wifi(NULL,rows)==3);
    assert(scan_reads==1 && calls.empty()); // browsing leaves the connection policy alone
    radio.clear();
    assert(!strcmp(rows[0].name,"Mesh") && rows[0].security==2 && rows[0].signal==-30);
    assert(full==rows[1].name && !strcmp(rows[2].name,"mesh"));
    assert(scan_wifi(NULL,rows)==0 && scan_reads==2);
    SlWlanEvent_t maximum={};
    maximum.Event=SL_WLAN_CONNECT_EVENT;
    maximum.EventData.STAandP2PModeWlanConnected.ssid_len=32;
    memset(maximum.EventData.STAandP2PModeWlanConnected.ssid_name,'x',32);
    SimpleLinkWlanEventHandler(&maximum);
    assert(WiFiClass::WiFi_status==WL_CONNECTED && strlen(WiFiClass::connected_ssid)==31);
    puts("PASS: stable full-length SSIDs, mesh duplicates, security pairing, bounded association cache");
    setup();
    /* The old IP event can arrive while disconnect is pending. */
    pending.push_back({5,3,old_address});
    assert(pico_network_connect_stored(&network));
    assert(joins==1 && dhcp_requests==1 && profile_deletes==1 && offline_polls>0 && now>=270);
    assert(WiFiClass::local_IP==new_address && !WiFiClass::_connecting);
    assert(calls.front()=="telemetry-network-change" && calls[1]=="policy:0" && calls.back()=="policy:1");
    assert(network_changes==1);
    PicoIpv4 ip={{0}},dns=ip,gate=ip,mask=ip;
    ip_configuration(NULL,&ip,&dns,&gate,&mask);
    assert(ip.octet[0]==10 && ip.octet[1]==0 && ip.octet[2]==1 && ip.octet[3]==143);
    assert(dns.octet[0]==1 && gate.octet[3]==1 && mask.octet[3]==0);
    for (int error=0;error<2;++error) {
        config_address=error?new_address:old_address;get_error=-error;
        ip_configuration(NULL,&ip,&dns,&gate,&mask);
        assert(ip.octet[0]==10 && ip.octet[3]==143);
        assert(pico_network_ipv4_is_zero(dns) && pico_network_ipv4_is_zero(gate) &&
               pico_network_ipv4_is_zero(mask));
    }
    puts("PASS: new SSID, late old lease, fresh DHCP, offline socket-service poll and address validation");

    setup();now=50;pico_local_service();
    assert(old_socket_open && offline_polls==0);
    immediate_disconnect=immediate_association=immediate_lease=require_offline_before_join=true;
    assert(pico_network_connect_stored(&network));
    assert(!old_socket_open && offline_polls==1 && now>=55 && network_changes==1);
    puts("PASS: immediate disconnect/reconnect observes offline through the real 5 ms service throttle");

    setup();immediate_association=true;
    assert(pico_network_connect_stored(&network) && now>=270 && joins==1);
    puts("PASS: callback during connect command is not overwritten; association waits for DHCP");

    setup();no_lease=true;
    assert(!pico_network_connect_stored(&network));
    assert(joins==2 && now>=90000 && now<92000 && WiFiClass::local_IP==0);
    expect_no_addresses();
    puts("PASS: stale NWP configuration cannot satisfy DHCP; missing lease fails within two bounded attempts");

    setup();no_association=true;
    assert(!pico_network_connect_stored(&network) && joins==2 && now<22000);
    setup();dhcp_error=-1;
    assert(!pico_network_connect_stored(&network) && joins==0);
    setup();join_error=-1;
    assert(!pico_network_connect_stored(&network) && joins==2 && !WiFiClass::_connecting);
    setup();profile_error=-1;
    assert(!pico_network_connect_stored(&network) && joins==2 && WiFiClass::local_IP==0);
    setup();policy_error=-1;
    assert(!pico_network_connect_stored(&network) && joins==0);
    // The rejected join left the original association intact. Status must
    // still show the address at which the local HTTP server is answering.
    ip_configuration(NULL,&ip,&dns,&gate,&mask);
    assert(ip.octet[0]==192 && ip.octet[3]==50);
    setup();disconnect_timeout=true;
    assert(!pico_network_connect_stored(&network) && joins==0 && now<13000);expect_no_addresses();
    setup();disconnect_error=SL_ERROR_WIFI_ALREADY_DISCONNECTED;
    assert(pico_network_connect_stored(&network));
    puts("PASS: driver errors and disconnect timeout never claim success or reuse the old address");

    setup();no_lease=true;cancelled_at=500;
    assert(!pico_network_connect_stored(&network) && joins==1 && WiFiClass::local_IP==0);
    setup();wlan(SL_WLAN_DISCONNECT_EVENT);
    assert(!WiFiClass::_connecting && WiFiClass::local_IP==0);
    ip_event(old_address);assert(WiFiClass::local_IP==0);
    wlan(SL_WLAN_CONNECT_EVENT);ip_event(new_address,SL_NETAPP_IPV6_IPACQUIRED_EVENT);
    assert(WiFiClass::local_IP==0);
    ip_event(new_address);assert(WiFiClass::local_IP==new_address);
    puts("PASS: cancellation disconnects; actual SDK callbacks clear leases and reject late/IPv6 events");
    const struct { const char *text; std::string bytes; } wep_cases[] = {
        {"abcde", "abcde"}, {"123456789abcd", "123456789abcd"},
        {"0041aB00fF", std::string("\x00\x41\xab\x00\xff", 5)},
        {"000102030405060708090a0b0c", std::string("\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c", 13)}
    };
    for (const auto &item : wep_cases) {
        setup();
        settings.wifi_security=5; strcpy(settings.wifi_password,item.text);
        expected_security=SL_SEC_TYPE_WEP; expected_key=item.bytes;
        assert(pico_network_connect_stored(&network) && joins==1 && profile_deletes==1);
    }
    for (const char *invalid : {"", "abcdef", "gg01020304", "0123456789012345678901234z"}) {
        setup();
        settings.wifi_security=5; strcpy(settings.wifi_password,invalid);
        assert(!pico_network_connect_stored(&network) && calls.empty());
    }
    puts("PASS: valid ASCII and hex WEP keys join and persist exact bytes once; invalid keys preserve the link");
    return 0;
}
