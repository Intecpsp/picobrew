#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
enum i2c_err_t {I2C_ERROR_OK=0,I2C_ERROR_ACK=2,I2C_ERROR_TIMEOUT=4};
class TwoWire {
public:
    unsigned deadline=0,reads=0,writes=0;std::uint32_t received=9;
    i2c_err_t read_error=I2C_ERROR_OK,write_error=I2C_ERROR_OK;
    std::vector<std::uint8_t> frame;
    void setTimeOut(std::uint16_t value){deadline=value;}
    i2c_err_t writeTransmission(std::uint16_t,std::uint8_t* data,std::uint16_t size,bool){++writes;frame.assign(data,data+size);return write_error;}
    i2c_err_t readTransmission(std::uint16_t,std::uint8_t* data,std::uint16_t size,bool,std::uint32_t* count){
        ++reads;std::memset(data,0x7f,size);*count=received;return read_error;}
};
inline void delay(unsigned long){}
