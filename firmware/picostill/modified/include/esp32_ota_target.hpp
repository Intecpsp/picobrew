#pragma once
#include <cstdint>
#define PB_FIELD_IDENTITY "PicoBrew OTA1|picostill|1.0.0"
#define PB_RECOVERY_IDENTITY "PicoBrew OTA1|picostill|1.0.0"
namespace picobrew { namespace esp32_ota {
constexpr char Model[] = "picostill";
constexpr char Version[] = "1.0.0";
constexpr char IdentityPrefix[] = "PicoBrew OTA1|picostill|";
constexpr char RecoveryPrefix[] = "PicoBrew OTA1|picostill|";
constexpr std::uint32_t RecoveryBytes = 0xf0000, SlotOffset = 0x100000, SlotBytes = 0xf0000;
constexpr std::uint32_t FilesystemOffset = 0x200000, FilesystemBytes = 0x200000;
}}
