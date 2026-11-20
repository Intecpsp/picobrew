#pragma once
#include "network_client_platform.hpp"
#include "zseries/network_probe.hpp"
namespace zseries { namespace oem { namespace esp32 {
class NetworkProbeBinding {
public:
    std::uint32_t mode=1;
    NetworkClientBinding connection;
    explicit NetworkProbeBinding(const ConsoleServices& diagnostics,Print* serial=nullptr)
        :connection(mode,diagnostics,serial){}
    NetworkProbeServices services();
};
}}} // namespace zseries::oem::esp32
