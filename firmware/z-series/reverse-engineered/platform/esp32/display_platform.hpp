#pragma once
#include "zseries/display.hpp"
#include <SPI.h>
namespace zseries { namespace oem { namespace esp32 {
DisplayIo display_io(SPIClass&) noexcept;
}}}
