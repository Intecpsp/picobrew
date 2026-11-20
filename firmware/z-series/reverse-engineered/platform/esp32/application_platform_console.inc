// Concrete implementations of the original platform-only console boundaries.
bool FirmwareApplication::scan_buses(){
    if(!log(ConsoleLevel::Info,"SYST","System.h",0x123,"scan()"))return false;
    const auto scan=[&](TwoWire& wire,BusPresence& found,const char* name,const std::uint8_t* expected,std::size_t count){
        if(!scope(true,"I2C","System.h",0x5b,"scan"))return false;
        found=BusPresence{};
        for(unsigned address=0;address<128;++address){wire.beginTransmission(static_cast<std::uint8_t>(address));found.found[address]=wire.endTransmission(true)==0;}
        for(std::size_t i=0;i<count;++i)if(!found.found[expected[i]])found.missing[found.missing_count++]=expected[i];
        const auto list=[&](const char* label,const std::uint8_t* addresses,std::size_t size){
            char buffer[129]{};std::size_t used=0;bool truncated=false;
            for(std::size_t i=0;i<size;++i){char item[8];::snprintf(item,sizeof item,"0x%02X ",addresses[i]);
                const auto available=128-used;const auto take=std::min<std::size_t>(5,available);
                if(take<5){if(!truncated&&!log(ConsoleLevel::Warning,"BUF","Buffer.h",0x66,"Buffer truncated"))return false;
                    truncated=true;if(!log(ConsoleLevel::Warning,"BUF","Buffer.h",0x6a,"Dropping %d bytes",static_cast<int>(5-take)))return false;}
                std::memcpy(buffer+used,item,take);used+=take;buffer[used]=0;
            }
            return log(ConsoleLevel::Info,"I2C",nullptr,0,"%s = %s",label,buffer);
        };
        std::array<std::uint8_t,128> addresses{};std::size_t size=0;
        for(unsigned i=0;i<128;++i)if(found.found[i])addresses[size++]=i;
        const bool info=log(ConsoleLevel::Info,"I2C",nullptr,0,"Name = %s",name)&&list("Expected",expected,count)&&
            list("Found",addresses.data(),size)&&list("Missing",found.missing.data(),found.missing_count)&&
            log(ConsoleLevel::Info,"I2C",nullptr,0,"hasError = %s",boolean(found.missing_count!=0));
        const bool closed=scope(false,"I2C","System.h",0x5b,"scan");
        if(!info||!closed)return false;
        if(found.missing_count&&!core.system_error){
            if(!log(ConsoleLevel::Error,"SYSST","SystemState.h",0x68,"setError %s",name))return false;
            copy_text(core.system_error_text,name);core.system_error=true;
        }
        return true;
    };
    const std::uint8_t sensors[]={0x19,0x34,0x76},controller[]={0x40};
    return scan(system.sensor_bus,system.sensors,"I2CA",sensors,3)&&scan(system.controller_bus,system.controller,"I2CB",controller,1);
}
bool FirmwareApplication::partition(const char* name,const esp_partition_t* value){
    if(!value)return log(ConsoleLevel::Warning,"SYST","System.h",0x9f,"%s is NULL",name);
    return log(ConsoleLevel::Info,"SYST","System.h",0xa1,"%s Type %d subtype %d (offset 0x%08x)",name,value->type,value->subtype,value->address);
}
bool FirmwareApplication::system_information(){
    const auto info=ConsoleLevel::Info;
    if(!log(info,"SYST",nullptr,0,"UID = %s",core.identifier.data())||!report_mac(*this,"SYST","ETH_MAC",core.ethernet_mac)||
       !log(info,"SYST",nullptr,0,"PRODUCT_NAME = %s","ZSeries")||!log(info,"SYST",nullptr,0,"PRODUCT_BACKEND = %s","www.picobrew.com")||
       !log(info,"SYST",nullptr,0,"PRODUCT_VERSION = %s","0.0.119")||!log(info,"SYST",nullptr,0,"GIT_BRANCH = %s","master")||
       !log(info,"SYST",nullptr,0,"GIT_HASH = %s","b6b00f995af567cc3010d27a1002c3789826e245")||
       !log(info,"SYST",nullptr,0,"BUILD_DATE = %s","Apr 17 2020")||!log(info,"SYST",nullptr,0,"BUILD_TIME = %s","08:34:08")||
       !log(info,"SYST",nullptr,0,"IDF_VERSION = %s",esp_get_idf_version())||
       !log(info,"SYST",nullptr,0,"tskKERNEL_VERSION_NUMBER = %s",tskKERNEL_VERSION_NUMBER)||
       !partition("FACTORY",system.factory_partition)||!partition("RUNNING",system.running_partition))return false;
    if(system.boot_partition!=system.running_partition){
        if(!log(ConsoleLevel::Error,"SYST","System.h",0x168,"Not running the configured partition")||!partition("CONFIGURED",system.boot_partition))return false;
    }
    if(!log(info,"SYST",nullptr,0,"Network = %s",to_string(static_cast<NetworkType>(core.network)))||
       !report_ip(*this,"SYST","WiFi.localIP()",static_cast<std::uint32_t>(WiFi.localIP()))||
       !report_ip(*this,"SYST","Ethernet.localIP()",esp32::ethernet_address())||
       !log(info,"SYST",nullptr,0,"StackHighWaterMark = %lu",static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)))||
       !log(info,"SYST",nullptr,0,"FreeHeapSize = %lu",static_cast<unsigned long>(esp_get_free_heap_size()))||
       !log(info,"SYST",nullptr,0,"MinimumEverFreeHeapSize = %lu",static_cast<unsigned long>(esp_get_minimum_free_heap_size())))return false;
    const auto& timing=core.timing;
    return log(info,"STAT","SystemState.h",0x23,"LastDate     = %lu",static_cast<unsigned long>(timing.last_date))&&
        log(info,"STAT","SystemState.h",0x24,"DurationLast = %lu ms",static_cast<unsigned long>(timing.duration))&&
        log(info,"STAT","SystemState.h",0x25,"DurationMin  = %lu ms",static_cast<unsigned long>(timing.minimum))&&
        log(info,"STAT","SystemState.h",0x26,"DurationMax  = %lu ms",static_cast<unsigned long>(timing.maximum))&&
        log(info,"STAT","SystemState.h",0x27,"DurationEma  = %4.2f ms",timing.average);
}
bool FirmwareApplication::wifi_information(bool sdk){
    const auto info=ConsoleLevel::Info;
    if(sdk){
        const auto status=static_cast<unsigned>(WiFi.status());const char* names[]={"IDLE_STATUS","NO_SSID_AVAIL","SCAN_COMPLETED","CONNECTED","CONNECT_FAILED","CONNECTION_LOST","DISCONNECTED"};
        const char* status_name=status<7?names[status]:status==255?"NO_SHIELD":"<WL_UNKNOWN>";
        if(!log(info,"WIFI","Wifi.h",0x109,"WiFi.getAutoReconnect() = %s",boolean(WiFi.getAutoReconnect()))||
           !log(info,"WIFI","Wifi.h",0x10a,"WL_STATUS = %s",status_name)||
           !log(info,"WIFI","Wifi.h",0x10b,"_target = %s",to_string(core.wifi.target))||
           !log(info,"WIFI","Wifi.h",0x10c,"Backoff = %s",boolean(core.wifi.backoff.active)))return false;
    }
    const auto& value=core.wifi.state;
    return log(info,"WIFI",nullptr,0,"State = %s",to_string(value.id))&&log(info,"WIFI",nullptr,0,"Rssi = %d",value.rssi)&&
        log(info,"WIFI",nullptr,0,"Level = %s",to_string(static_cast<WiFiLevel>(value.level)))&&
        report_ip(*this,"WIFI","LocalIP",value.local_address)&&report_ip(*this,"WIFI","GatewayIP",value.gateway_address)&&
        report_ip(*this,"WIFI","TestHostIP",value.test_host_address)&&log(info,"WIFI",nullptr,0,"SSID_CONNECTED = %s",value.connected.ssid.data());
}
bool FirmwareApplication::core_information(){
    return system_information()&&core.eeprom.command(EepromCommand::Info)==EepromResult::Ok&&display_console.information()&&
        report_mac(*this,"CORE","WIFI_MAC",core.wifi.mac_address)&&wifi_information(false)&&
        log(ConsoleLevel::Info,"CORE",nullptr,0,"STATE = %s",to_string(core.core.state.id))&&
        core.serial_logger.information()==SerialLogResult::Ok&&log(ConsoleLevel::Info,"CORE",nullptr,0,"BARE_PCB = %s","FALSE");
}
bool FirmwareApplication::sizes(){
    // Introspection must describe this rebuilt program's real storage. The
    // original C++ ABI/object sizes are not fabricated as live sizeof values.
    struct Item {const char* name;std::size_t bytes;std::uint32_t line;};
#define Z_SIZE(type,line) {"sizeof(" #type ")",sizeof(type),line}
    const Item items[]={Z_SIZE(CoreApplication,0x172),Z_SIZE(HttpClient,0x173),Z_SIZE(WiFiClient,0x174),Z_SIZE(WiFiClientSecure,0x175),
        Z_SIZE(EthernetTransport,0x176),Z_SIZE(ClientFactory,0x177),Z_SIZE(ProgramBuffer,0x178),Z_SIZE(DisplayState,0x179),
        Z_SIZE(WifiManager,0x17b),Z_SIZE(System,0x17c),Z_SIZE(z_msp_client,0x17d),Z_SIZE(EepromData,0x17e),Z_SIZE(EepromStore,0x17f),
        Z_SIZE(PhysicalButton,0x180),Z_SIZE(TemperatureAcquisition,0x181),Z_SIZE(Buzzer,0x182),Z_SIZE(HeaterController,0x183),
        Z_SIZE(SerialReaderState,0x184),Z_SIZE(HeadApplication,0x185),Z_SIZE(FirmwareUpdate,0x186),Z_SIZE(BinaryDownload,0x187),
        Z_SIZE(ProgramRuntime,0x188),Z_SIZE(HttpBackend,0x189),Z_SIZE(SerialLogger,0x18a),Z_SIZE(RegistrationState,0x18b),
        Z_SIZE(ProgramDownloadState,0x18c),Z_SIZE(SessionLogger,0x18d),Z_SIZE(LocalHttpParser,0x18e),Z_SIZE(ZBackend,0x191),
        Z_SIZE(PicoStillController,0x192),Z_SIZE(StillAuthorization,0x196),Z_SIZE(StillClient,0x197),Z_SIZE(CoreEvent,0x199),
        Z_SIZE(CoreCommand,0x19a),Z_SIZE(HeadUiCommandData,0x19b),Z_SIZE(CoreRuntimeEvent,0x19c),Z_SIZE(BackendState,0x19e),
        Z_SIZE(HeaterState,0x19f),Z_SIZE(z_msp_state,0x1a0),Z_SIZE(ProgramRuntimeState,0x1a1),Z_SIZE(HeaterSensors,0x1a2),
        Z_SIZE(SystemLoopTiming,0x1a3),Z_SIZE(CoreSession,0x1a4),Z_SIZE(CoreFlowState,0x1a5),Z_SIZE(PicoStillSensors,0x1a6),
        Z_SIZE(PicoStillState,0x1a7),Z_SIZE(PicoStillStatistics,0x1a8),Z_SIZE(HeadScreenRequest,0x1aa),Z_SIZE(HeadMenuRenderer,0x1ab),
        Z_SIZE(HeadTimer,0x1ac),Z_SIZE(HeadScreens,0x1ad),Z_SIZE(HeadScreenState,0x1ae),Z_SIZE(HeadParagraphState,0x1b0),
        Z_SIZE(HeadScrollData,0x1b1),Z_SIZE(HeadWrappedText,0x1b2),Z_SIZE(HeadWidgetPainter,0x1b3),Z_SIZE(double,0x1b6),
        Z_SIZE(float,0x1b7),Z_SIZE(bool,0x1b8),Z_SIZE(mbedtls_pk_context,0x1ba),Z_SIZE(mbedtls_x509_crt,0x1bb)};
#undef Z_SIZE
    for(const auto& item:items)if(!log(ConsoleLevel::Info,"CORE","Core.h",item.line,"%-35s %lu",item.name,static_cast<unsigned long>(item.bytes)))return false;
    return true;
}
bool FirmwareApplication::ping_test(){
    struct Nested {
        FirmwareApplication& app;PingerBinding binding;std::array<ConsoleEntry,5> entries;
        ConsoleShell shell;
        explicit Nested(FirmwareApplication& value):app(value),binding(value.diagnostics),shell("Ping",entries.data(),entries.size(),value.diagnostics){
            binding.registry_context=this;binding.register_commands=[](void* p){auto& n=*static_cast<Nested*>(p);
                return n.shell.add_command(true,"i","Display Ping information",&n,[](void*,const char*){return true;})==ConsoleResult::Ok&&
                    n.shell.add_command(true,"g","ping 8.8.4.4 (aka google)",&n,[](void* p,const char*){bool started=false;return static_cast<Nested*>(p)->binding.pinger.ping(0x04040808,started)==PingerResult::Ok;})==ConsoleResult::Ok&&
                    n.shell.add_command(false,"name ","ping given host name",&n,[](void* p,const char* text){bool started=false;return static_cast<Nested*>(p)->binding.pinger.hostname(text,started)==PingerResult::Ok;})==ConsoleResult::Ok&&
                    n.shell.add_command(false,"ip ","ping given IPAddress",&n,[](void* p,const char* text){bool started=false;return static_cast<Nested*>(p)->binding.pinger.address(text,started)==PingerResult::Ok;})==ConsoleResult::Ok&&
                    n.shell.add_command(true,"test","Run Pinger tests",&n,[](void* p,const char*){return static_cast<Nested*>(p)->app.ping_test();})==ConsoleResult::Ok;
            };
        }
    };
    if(!test_scope(true,"PING","Pinger.h",0xb5,"Pinger::setup()"))return false;
    bool ok;{Nested nested(*this);ok=nested.binding.setup()==PingerResult::Ok;}
    if(!test_scope(false,"PING","Pinger.h",0xb5,"Pinger::setup()")||!ok||
       !test_scope(true,"PING","Pinger.h",0xba,"Pinger::ping()"))return false;
    {Nested nested(*this);ok=nested.binding.setup()==PingerResult::Ok;bool started=false;
        if(ok)ok=nested.binding.pinger.ping(0x04040808,started)==PingerResult::Ok;
        if(ok)while(nested.binding.pinger.remaining>0)::yield();
    }
    return test_scope(false,"PING","Pinger.h",0xba,"Pinger::ping()")&&ok;
}
bool FirmwareApplication::invoke_console(std::uint32_t callback,const char* argument){
    switch(callback){
    case 0x400dbc2c:return system_information();
    case 0x40113324:return scan_buses();
    case 0x400d5f78:return select_factory();
    case 0x400d5ff4:return restart();
    case 0x400d60ec:return ethernet_connect(10000);
    case 0x400dbccc:return core.buzzer.set_alarm(core.alarm,eeprom_text_boolean(argument))==BuzzerResult::Ok;
    case 0x401bdbc0:for(;;)asm volatile("nop");
    case 0x400efcf0:return core_information();
    case 0x400d5fe8:return screen_power(false)&&restart();
    case 0x400d9164:return sizes();
    case 0x400d2190:return set_core_heater_timers(core.timers,eeprom_text_boolean(argument),core.ports.timers);
    case 0x400edd04:
        return log(ConsoleLevel::Warning,"CORE","Core.h",0x138,"_factoryReset")&&
            set_core_heater_timers(core.timers,false,core.ports.timers)&&core.eeprom.reset()==EepromResult::Ok&&
            select_factory()&&screen_power(false)&&restart();
    case 0x400d0a48: ::delay(static_cast<std::uint32_t>(::atoi(argument?argument:"")));return true;
    case 0x401beec4:return true; // The entire original callback is RETW.
    case 0x400e15c4:{bool started=false;return pinger.pinger.ping(0x04040808,started)==PingerResult::Ok;}
    case 0x400e171c:{bool started=false;return pinger.pinger.hostname(argument,started)==PingerResult::Ok;}
    case 0x400e17b8:{bool started=false;return pinger.pinger.address(argument,started)==PingerResult::Ok;}
    case 0x400f7204:case 0x400f72dc:return ping_test();
    case 0x4011ed44:case 0x4010a364:case 0x4011bffc:case 0x400dd278:
        return console.invoke_original(callback,argument)==ConsoleResult::Ok;
    case 0x401024a8:return binary_download_console_test(test_http_services,diagnostics,console.hooks);
    case 0x40110f80:{
        HttpClientTestResources resources;resources.text=original_credentials::http_client_test_text;
        return http_client_console_test(test_http_services,diagnostics,console.hooks,resources)==ConsoleResult::Ok;
    }
    case 0x4011b6cc:{
        HttpClientTestResources resources;resources.text=original_credentials::http_client_test_text;
        return http_backend_network_console_test(core.backend,core.identifier,diagnostics,console.hooks,resources);
    }
    case 0x400da1f4:return wifi_information(true);
    case 0x400d09bc:WiFi.setAutoReconnect(true);return true;
    case 0x400d09cc:WiFi.setAutoReconnect(false);return true;
    case 0x400d77b0:return azure.information();
    case 0x400fa7ec:return azure.developer_tests();
    case 0x400fec74:return azure_console_self_test(core.azure,diagnostics,console.hooks)==ConsoleResult::Ok;
    case 0x400edbfc:return save_configuration();
    case 0x400edc9c:return set_core_heater_timers(core.timers,false,core.ports.timers)&&core.eeprom.reset()==EepromResult::Ok&&screen_power(false)&&restart();
    case 0x400e7bbc:observe_head();return head.console(HeadConsoleCommand::Splash)==HeadApplicationResult::Ok;
    case 0x4010369c:observe_head();return head.console(HeadConsoleCommand::Hardware)==HeadApplicationResult::Ok;
    case 0x40103740:observe_head();return head.console(HeadConsoleCommand::Menu)==HeadApplicationResult::Ok;
    case 0x4010376c:observe_head();return head.console(HeadConsoleCommand::UiMenu)==HeadApplicationResult::Ok;
    case 0x400fe350:case 0x400fe36c:observe_head();return head.console(HeadConsoleCommand::Tests)==HeadApplicationResult::Ok;
    case 0x400fea6c:observe_head();return head.console(HeadConsoleCommand::Slideshows)==HeadApplicationResult::Ok;
    case 0x400d7020:case 0x400d1bfc:case 0x400d1b4c:case 0x400d1b5c:case 0x400e780c:
    case 0x400d0908:case 0x400d0920:case 0x400fce34:case 0x400e78b4:case 0x400e78c8:
    case 0x400e78dc:case 0x400e78f0:case 0x400e7db4:case 0x400e8510:case 0x400e85cc:
    case 0x400e8994:case 0x400e95b0:case 0x400ea520:case 0x400eaf50:case 0x400d09a4:
        return display_console.invoke(callback,argument);
    default:return false; // Diagnostic clusters are installed by their recovered owner.
    }
}
