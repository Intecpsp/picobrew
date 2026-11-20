// Link-only integration probe. This is deliberately not an installable app.
// Its only purpose is to retain Core's real component graph in the target ELF
// so unresolved symbols/ABI mismatches cannot hide in a static object archive.
#include "zseries/core_application.hpp"
#include "esp32_platform.hpp"
#include "network_probe_platform.hpp"
#include "azure_mqtt_platform.hpp"
#include <new>
using namespace zseries::oem;
extern "C" void app_main(){
    CoreEventMailbox* mailbox=new(std::nothrow) CoreEventMailbox;
    if(!mailbox)return;
    CoreApplication* app=new(std::nothrow) CoreApplication(*mailbox);
    if(!app)return;
    // No fake bindings: setup correctly reports MissingBinding and no heater
    // initialization can occur. Volatile results retain both actual call graphs.
    volatile ApplicationResult setup=app->setup();
    volatile ApplicationResult tick=app->tick(0);
    (void)setup;(void)tick;
    volatile bool second=heater_tick_1s(app->heater,app->temperatures,app->safeties.thermal,
        original_thermal_limits(),0,app->ports.heater_gpio);
    volatile bool fast=heater_tick_20ms(app->heater,app->ports.heater_gpio);
    (void)second;(void)fast;
    esp32::System system;
    esp32::NetworkProbeBinding probe(app->ports.diagnostics);
    bool connected=false;
    volatile NetworkResult result=check_network_connectivity(0,probe.services(),connected);
    (void)result;
    esp32::AzureMqttBinding mqtt(app->network,app->ports.diagnostics);
    mqtt.attach(app->azure);
    delete app;delete mailbox;
}
