#pragma once
#include "network_client_platform.hpp"
#include "zseries/mqtt_client.hpp"
#include "zseries/backend_console.hpp"

namespace zseries { namespace oem { namespace esp32 {
// Owns a distinct factory/socket for the Azure MQTT connection. HTTP sessions
// retain their own NetworkClientBinding instances and cannot invalidate this one.
class AzureMqttBinding {
public:
    AzureMqttBinding(std::uint32_t& network_mode,const ConsoleServices&,Print* serial=nullptr);
    NetworkClientBinding network;
    MqttClient mqtt;
    ConsoleServices diagnostics;
    AzureService* azure=nullptr;
    MqttResult protocol_result=MqttResult::Ok;
    NetworkResult factory_result=NetworkResult::Ok;
    int hmac_error=0;
    void attach(AzureService&);
    AzureConsoleCallbacks console_callbacks() noexcept;
    bool information(); // original 0x400d7744
    bool developer_tests(); // original 0x400fa684, using caller-supplied key
    bool diagnostic(std::uint32_t line);
    bool assertion(std::uint32_t line);
    bool perform(const AzureAction&,std::uint32_t&);
    bool transport_ok() const noexcept;
};
}}} // namespace zseries::oem::esp32
