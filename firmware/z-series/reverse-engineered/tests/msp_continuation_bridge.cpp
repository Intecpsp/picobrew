#include "zseries/msp_console_tests.hpp"
#include "zseries/msp_images.hpp"
#include "zseries/application_console.hpp"
using namespace zseries::oem;
extern "C" const z_msp_image* msp_production_image(){return &production_msp_image;}
extern "C" const z_msp_image* msp_unit_image(){return &unit_test_msp_image;}
extern "C" bool msp_continuation_test(z_msp_client* client,std::uint32_t callback,
    bool(*scope)(void*,bool,const char*,const char*,std::uint32_t,const char*)){
    ConsoleServices services;services.context=const_cast<z_msp_diagnostics*>(client->diagnostics);
    services.assertion=[](void*p,const char*f,std::uint32_t l,const char*e){const auto& d=*static_cast<const z_msp_diagnostics*>(p);return d.assertion&&d.assertion(d.context,f,l,e);};
    services.log=[](void*p,ConsoleLevel level,const char*t,const char*f,std::uint32_t l,const char*m){const auto& d=*static_cast<const z_msp_diagnostics*>(p);return d.log&&d.log(d.context,static_cast<unsigned>(level),t,f,l,m);};
    ApplicationConsoleHooks hooks;hooks.context=client->diagnostics?client->diagnostics->context:nullptr;hooks.test_scope=scope;
    return msp_console_self_test(callback,*client,production_msp_image,unit_test_msp_image,services,hooks);
}
