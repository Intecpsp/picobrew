#pragma once

namespace picobrew { namespace standalone { namespace esp32 {
// Modified replacement for the pinned SDK's initArduino(). Preserves CPU,
// PSRAM, log, Bluetooth-memory, init and variant setup. Returns the exact
// nvs_flash_init status and never erases an existing NVS partition. Call once
// from app_main before creating the appliance task; failure must inhibit its
// controller initialization. The stock SDK main/initArduino must not be used.
int initialize_arduino() noexcept;
int nvs_initialization_status() noexcept;
}}}
