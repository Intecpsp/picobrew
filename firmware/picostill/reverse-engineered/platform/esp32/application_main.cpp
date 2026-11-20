// Reconstructed PicoStill setup/loop. Linked with the pinned SDK's unmodified
// Arduino main.cpp, which owns app_main and the 8192-byte loop task.
#include "picostill/machine.hpp"
#include "arduino_ports.hpp"
#include "nvs_ports.hpp"
#include "wifi_ports.hpp"
#include "http_ports.hpp"
#include "outbound_ports.hpp"
#include "system_ports.hpp"
namespace {
using namespace picostill::oem;
ArduinoPorts hardware_ports;
NvsPorts storage_ports;
ArduinoWifiPorts wifi_ports;
ArduinoHttpRuntime http_runtime(hardware_ports);
ArduinoHttpListener listener;
ArduinoOutboundClient backend_client,update_client;
ArduinoUpdatePorts update_ports;
ArduinoSystemPorts system_ports(hardware_ports,wifi_ports);
Machine machine(hardware_ports,hardware_ports,hardware_ports,storage_ports,wifi_ports,http_runtime,listener,backend_client,update_client,update_ports,system_ports,system_ports);
}
void setup(){Serial.begin(115200,SERIAL_8N1,-1,-1,false,20000);machine.setup();}
void loop(){::yield();machine.poll();}
