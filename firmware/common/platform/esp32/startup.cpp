// Derived from Arduino-ESP32 1.0.1 esp32-hal-misc.c::initArduino.
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
// Licensed under Apache License 2.0: http://www.apache.org/licenses/LICENSE-2.0
// The automatic erase/retry branch is deliberately absent in this modified
// profile. An initialization failure is returned to the appliance owner.
#include "startup.hpp"
#include <Arduino.h>
#include <sdkconfig.h>
#include <esp_log.h>
#include <nvs_flash.h>
#ifdef CONFIG_BT_ENABLED
#include <esp_bt.h>
extern "C" bool btInUse(); // weak C symbol in the pinned esp32-hal-misc.c
#endif

namespace picobrew { namespace standalone { namespace esp32 {
namespace { int nvs_status = -1; }
int nvs_initialization_status() noexcept { return nvs_status; }
int initialize_arduino() noexcept {
#ifdef F_CPU
    setCpuFrequencyMhz(F_CPU / 1000000);
#endif
#if CONFIG_SPIRAM_SUPPORT
    psramInit();
#endif
    esp_log_level_set("*", static_cast<esp_log_level_t>(CONFIG_LOG_DEFAULT_LEVEL));
    nvs_status = nvs_flash_init();
#ifdef CONFIG_BT_ENABLED
    if (!btInUse()) esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
#endif
    init();
    initVariant();
    return nvs_status;
}
}}}
