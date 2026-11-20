// Original Core Network/Int/All diagnostics and backend network test bodies.
#pragma once
#include "zseries/application_console.hpp"
#include "zseries/http_backend.hpp"
#include "zseries/http_client_console_tests.hpp"
#include <array>

namespace zseries { namespace oem {
struct NetworkConsolePorts {
    void* context = nullptr;
    bool (*ethernet_address)(void*, std::uint32_t&) = nullptr;
    bool (*wifi_address)(void*, std::uint32_t&) = nullptr;
    std::uint32_t* selected_network = nullptr;
};
bool binary_download_console_test(HttpServices, const ConsoleServices&,
                                  const ApplicationConsoleHooks&);
bool http_backend_network_console_test(HttpBackend&, std::array<char,13>& shared_identifier,
    const ConsoleServices&, const ApplicationConsoleHooks&, const HttpClientTestResources&);
bool core_network_console_self_test(const NetworkConsolePorts&, const ConsoleServices&,
                                    const ApplicationConsoleHooks&);
bool core_integration_console_self_test(const ConsoleServices&, const ApplicationConsoleHooks&);
bool core_all_console_self_test(const ConsoleServices&, const ApplicationConsoleHooks&);
bool core_console_test(std::uint32_t original_callback, CoreApplication&,
                       const ConsoleServices&, const ApplicationConsoleHooks&);
}} // namespace zseries::oem
