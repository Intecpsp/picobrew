#pragma once
#include "picostill/storage.hpp"
#include <Arduino.h>

namespace picostill { namespace oem {
class NvsPorts final:public StoragePorts {
public:
    explicit NvsPorts(HardwareSerial& serial=Serial) noexcept:serial_(serial) {}
    int open(const char* name,std::uint32_t& handle) override;
    int get_blob(std::uint32_t handle,const char* key,void* bytes,std::size_t& size) override;
    int set_blob(std::uint32_t handle,const char* key,const void* bytes,std::size_t size) override;
    int erase_key(std::uint32_t handle,const char* key) override;
    int commit(std::uint32_t handle) override;
    std::uint8_t* allocate(std::size_t size) override;
    void release(std::uint8_t* bytes,bool array_delete) override;
    void error(unsigned line,int status,std::uint32_t old_size,std::uint32_t new_size) override;
    void key_size(std::uint32_t size) override;
    void ready() override;
private:
    HardwareSerial& serial_;
};
}} // namespace picostill::oem
