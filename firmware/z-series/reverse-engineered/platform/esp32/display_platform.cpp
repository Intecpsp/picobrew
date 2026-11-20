#include "display_platform.hpp"
#include <Arduino.h>
namespace zseries { namespace oem { namespace esp32 {
DisplayIo display_io(SPIClass& spi) noexcept {
    DisplayIo io;
    io.context=&spi;
    io.pin_mode=[](void*,std::uint8_t pin,std::uint8_t mode){pinMode(pin,mode);};
    io.digital_write=[](void*,std::uint8_t pin,std::uint8_t value){digitalWrite(pin,value);};
    io.spi_byte=[](void* context,std::uint8_t value){static_cast<SPIClass*>(context)->transfer(value);};
    io.spi_bytes=[](void* context,const std::uint8_t* data,std::size_t count){
        static_cast<SPIClass*>(context)->transferBytes(const_cast<std::uint8_t*>(data),nullptr,count);
    };
    return io;
}
}}}
