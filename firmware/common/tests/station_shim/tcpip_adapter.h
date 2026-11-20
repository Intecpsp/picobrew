#pragma once
#define TCPIP_ADAPTER_IF_STA 0
#define ESP_ERR_TCPIP_ADAPTER_DHCP_ALREADY_STARTED 0x5003
int tcpip_adapter_dhcpc_start(int);
