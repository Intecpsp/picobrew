#include "zseries/backend_console.hpp"
namespace zseries { namespace oem {
namespace {
bool server_info(void* p,const char*){auto& binding=*static_cast<HttpServerConsoleCallbacks*>(p);return binding.information&&binding.information(binding.context);}
bool server_start(void* p,const char*){auto* server=static_cast<HttpServerConsoleCallbacks*>(p)->server;return server&&server->enable(true)==LocalHttpResult::Ok;}
bool server_stop(void* p,const char*){auto* server=static_cast<HttpServerConsoleCallbacks*>(p)->server;return server&&server->enable(false)==LocalHttpResult::Ok;}
bool backend_info(void* p,const char*){auto& binding=*static_cast<BackendConsoleCallbacks*>(p);return binding.information&&binding.information(binding.context);}
bool azure_info(void* p,const char*){auto& b=*static_cast<AzureConsoleCallbacks*>(p);return b.information&&b.information(b.context);}
bool azure_tests(void* p,const char*){auto& b=*static_cast<AzureConsoleCallbacks*>(p);return b.tests&&b.tests(b.context);}
bool azure_connect(void* p,const char*){return static_cast<AzureConsoleCallbacks*>(p)->azure->connect()==BackendResult::Ok;}
bool azure_disconnect(void* p,const char*){static_cast<AzureConsoleCallbacks*>(p)->azure->disconnect();return true;}
bool azure_start(void* p,const char*){return static_cast<AzureConsoleCallbacks*>(p)->azure->session_begin()==BackendResult::Ok;}
bool azure_stop(void* p,const char*){return static_cast<AzureConsoleCallbacks*>(p)->azure->session_end()==BackendResult::Ok;}
bool azure_send(void* p,const char*){return static_cast<AzureConsoleCallbacks*>(p)->azure->publish_state()==BackendResult::Ok;}
bool backend_tests(void* p,const char*){auto& binding=*static_cast<BackendConsoleCallbacks*>(p);return binding.tests&&binding.tests(binding.context);}
}
ConsoleResult register_azure_console(ConsoleShell& shell,AzureConsoleCallbacks& bindings){
    if(!bindings.azure||!bindings.information||!bindings.tests)return ConsoleResult::MissingService;
    const char* tokens[]={"i","c","d","start","stop","send","test"};
    const char* descriptions[]={"Display Azure information","Connects to Azure","Disconnects from Azure","Simulate session start","Simulate session stop","Sends message to IotHub","Runs Azure unit tests"};
    const ConsoleCallback callbacks[]={azure_info,azure_connect,azure_disconnect,azure_start,azure_stop,azure_send,azure_tests};
    for(unsigned i=0;i<7;++i){const auto result=shell.add_command(true,tokens[i],descriptions[i],&bindings,callbacks[i]);if(result!=ConsoleResult::Ok)return result;}
    return ConsoleResult::Ok;
}
ConsoleResult register_http_server_console(ConsoleShell& shell,HttpServerConsoleCallbacks& bindings){
    if(!bindings.server||!bindings.information)return ConsoleResult::MissingService;
    auto result=shell.add_command(true,"i","Display HttpServer information",&bindings,server_info);if(result!=ConsoleResult::Ok)return result;
    result=shell.add_command(true,"start","Starts HttpServer",&bindings,server_start);if(result!=ConsoleResult::Ok)return result;
    return shell.add_command(true,"stop","Stops HttpServer",&bindings,server_stop);
}
ConsoleResult register_http_backend_console(ConsoleShell& shell,BackendConsoleCallbacks& bindings){
    if(!bindings.information||!bindings.tests)return ConsoleResult::MissingService;
    auto result=shell.add_command(true,"i","Display information",&bindings,backend_info);if(result!=ConsoleResult::Ok)return result;
    return shell.add_command(true,"tests","Run HttpBackend unit tests",&bindings,backend_tests);
}
ConsoleResult register_z_backend_console(ConsoleShell& shell,BackendConsoleCallbacks& bindings){
    if(!bindings.information||!bindings.tests)return ConsoleResult::MissingService;
    auto result=shell.add_command(true,"i","Display backend information",&bindings,backend_info);if(result!=ConsoleResult::Ok)return result;
    return shell.add_command(true,"tests","Run ZBackendT unit tests",&bindings,backend_tests);
}
}} // namespace zseries::oem
