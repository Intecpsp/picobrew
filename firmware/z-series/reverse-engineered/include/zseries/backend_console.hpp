#pragma once
#include "zseries/console.hpp"
#include "zseries/local_http_server.hpp"
#include "zseries/azure.hpp"
namespace zseries { namespace oem {
struct BackendConsoleCallbacks {
    void* context=nullptr;
    bool (*information)(void*)=nullptr;
    bool (*tests)(void*)=nullptr;
};
struct HttpServerConsoleCallbacks {
    LocalHttpServer* server=nullptr;
    void* context=nullptr;
    bool (*information)(void*)=nullptr;
};
struct AzureConsoleCallbacks {
    AzureService* azure=nullptr;
    void* context=nullptr;
    bool (*information)(void*)=nullptr;
    bool (*tests)(void*)=nullptr;
};
// Construct persistent child shells with these names and caller-owned buffers,
// then attach them to the System shell. Callbacks must outlive registrations.
constexpr const char* AzureConsoleName="Azure";
constexpr const char* HttpServerConsoleName="HttpServer";
constexpr const char* HttpBackendConsoleName="HttpBackend";
constexpr const char* ZBackendConsoleName="ZBackendT";
ConsoleResult register_azure_console(ConsoleShell&,AzureConsoleCallbacks&); // 0x400ff6e8
ConsoleResult register_http_server_console(ConsoleShell&,HttpServerConsoleCallbacks&); // 0x400f7dc8
ConsoleResult register_http_backend_console(ConsoleShell&,BackendConsoleCallbacks&); // 0x401122a0
ConsoleResult register_z_backend_console(ConsoleShell&,BackendConsoleCallbacks&); // 0x40112378
}} // namespace zseries::oem
