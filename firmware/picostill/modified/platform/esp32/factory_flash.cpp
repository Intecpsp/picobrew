#include <esp32_ota_target.hpp>
extern "C" const char picostill_update_identity[64]
    __attribute__((section(".rodata_custom_desc"), used)) = PB_FIELD_IDENTITY;
