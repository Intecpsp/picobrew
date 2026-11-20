// Z119 ZClient::checkIsConnected, including its Ethernet reachability probe.
#pragma once
#include "zseries/network_client.hpp"
namespace zseries { namespace oem {
struct NetworkProbeServices {
    void* context=nullptr;
    bool (*wifi_status)(void*,std::int32_t&)=nullptr;
    // Acquire a plain Ethernet client from the probe's own factory. A completed
    // acquisition can report unavailable; that is an OEM false result.
    bool (*ethernet_acquire)(void*,bool& available)=nullptr;
    bool (*ethernet_connect)(void*,const char*,std::uint16_t,std::int32_t&)=nullptr;
    bool (*ethernet_stop)(void*)=nullptr;
    ConsoleServices diagnostics;
};
// Original 0x400ed7d8 and 0x400ed728. No DNS/socket operation is performed on
// WiFi: the original accepts every status except 1,4,5,6,255.
NetworkResult check_network_connectivity(std::uint32_t type,const NetworkProbeServices&,bool& connected);
}} // namespace zseries::oem
