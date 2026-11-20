#include "zseries/msp_console_tests.hpp"
#include "zseries/application_console.hpp"
namespace zseries { namespace oem {
namespace {
struct TestScope {
    const ApplicationConsoleHooks& hooks;std::uint32_t line;const char* name;bool active=false;
    TestScope(const ApplicationConsoleHooks& value,std::uint32_t source,const char* label):hooks(value),line(source),name(label){}
    bool begin(){if(!hooks.test_scope)return false;active=hooks.test_scope(hooks.context,true,"MSP","Msp.h",line,name);return active;}
    bool end(){if(!active)return false;active=false;return hooks.test_scope(hooks.context,false,"MSP","Msp.h",line,name);}
    ~TestScope(){if(active)hooks.test_scope(hooks.context,false,"MSP","Msp.h",line,name);}
};
bool assertion(const ConsoleServices& services,bool condition,std::uint32_t line,const char* expression){
    return condition||(services.assertion&&services.assertion(services.context,"Msp.h",line,expression));
}
bool versions(z_msp_client& client,unsigned count,const ApplicationConsoleHooks& hooks){
    TestScope scope(hooks,0x202,"testAppGetVersion");if(!scope.begin())return false;
    for(unsigned i=0;i<count;++i)if(z_msp_get_version(&client)<0)return false;
    return scope.end();
}
bool switching(z_msp_client& client,unsigned count,const ApplicationConsoleHooks& hooks){
    TestScope scope(hooks,0x1f8,"testAppBootSwitching");if(!scope.begin())return false;
    for(unsigned i=0;i<count;++i)if(z_msp_switch_to_boot(&client)<0||z_msp_start_app(&client)<0||z_msp_get_version(&client)<0)return false;
    return scope.end();
}
bool update(z_msp_client& client,unsigned count,const z_msp_image& production,const z_msp_image& unit,
            const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    if(!client.diagnostics||!client.diagnostics->assertion_count||!client.diagnostics->watchdog_reset)return false;
    TestScope scope(hooks,0x20a,"testUpdate");if(!scope.begin())return false;
    for(unsigned i=0;i<count;++i){
        if(z_msp_switch_to_boot(&client)<0||z_msp_update(&client,&unit)<0||z_msp_start_app(&client)<0||z_msp_get_version(&client)<0)return false;
        if(!assertion(services,client.state->version_major==255,0x211,"_mspState.versionMaj == 255")||
           !assertion(services,client.state->version_minor==255,0x212,"_mspState.versionMin == 255"))return false;
        if(z_msp_setup(&client,&production)<0)return false;
        if(!assertion(services,client.state->version_major==1,0x216,"_mspState.versionMaj == APP_VERSION_MAJ")||
           !assertion(services,client.state->version_minor==0,0x217,"_mspState.versionMin == APP_VERSION_MIN"))return false;
        // Original BLTI at400dd221 compares the shared counter as signed.
        if(static_cast<std::int32_t>(client.diagnostics->assertion_count(client.diagnostics->context))>0)break;
        if(!client.diagnostics->watchdog_reset(client.diagnostics->context))return false;
    }
    return scope.end();
}
bool bootstrap(z_msp_client& client,const z_msp_image& production,const ApplicationConsoleHooks& hooks){
    TestScope scope(hooks,0x1f0,"testBootstrap");if(!scope.begin())return false;
    if(z_msp_switch_to_boot(&client)<0||z_msp_erase_app(&client)<0||z_msp_setup(&client,&production)<0)return false;
    return scope.end();
}
bool errors(z_msp_client& client,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    TestScope scope(hooks,0x223,"testErrors");if(!scope.begin())return false;
    if(z_msp_send(&client,Z_MSP_TEST_SET_ERRORS,0xdeadbeef,4)<0||
       !assertion(services,client.state->errors==0xdeadbeef,0x225,"0xdeadbeef == _mspState.errors"))return false;
    if(z_msp_send(&client,Z_MSP_CLEAR_ERRORS,0,0)<0||
       !assertion(services,client.state->errors==0,0x227,"0x0 == _mspState.errors"))return false;
    return scope.end();
}
}
bool msp_console_self_test(std::uint32_t callback,z_msp_client& client,const z_msp_image& production,
    const z_msp_image& unit,const ConsoleServices& services,const ApplicationConsoleHooks& hooks){
    if(!client.diagnostics)return false;
    switch(callback){
    case 0x400dc1a4:return versions(client,200,hooks);
    case 0x400dc970:return switching(client,50,hooks);
    case 0x400dd268:return update(client,10,production,unit,services,hooks);
    case 0x400dd29c:return bootstrap(client,production,hooks)&&switching(client,10,hooks)&&versions(client,10,hooks)&&update(client,2,production,unit,services,hooks)&&errors(client,services,hooks);
    // Original named helpers are also exposed for the aggregate diagnostic
    // owner and narrow execution fixtures, without altering their loop counts.
    case 0x400dd0f0:return bootstrap(client,production,hooks);
    case 0x400dccf0:return errors(client,services,hooks);
    default:return false;
    }
}
}} // namespace zseries::oem
