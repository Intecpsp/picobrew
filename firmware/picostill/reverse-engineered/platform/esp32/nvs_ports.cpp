#include "nvs_ports.hpp"
#include <nvs.h>
#include <cstdlib>

namespace picostill { namespace oem {
int NvsPorts::open(const char* name,std::uint32_t& handle) {return nvs_open(name,NVS_READWRITE,&handle);}
int NvsPorts::get_blob(std::uint32_t handle,const char* key,void* bytes,std::size_t& size) {return nvs_get_blob(handle,key,bytes,&size);}
int NvsPorts::set_blob(std::uint32_t handle,const char* key,const void* bytes,std::size_t size) {return nvs_set_blob(handle,key,bytes,size);}
int NvsPorts::erase_key(std::uint32_t handle,const char* key) {return nvs_erase_key(handle,key);}
int NvsPorts::commit(std::uint32_t handle) {return nvs_commit(handle);}
std::uint8_t* NvsPorts::allocate(std::size_t size) {
    // Zero allocator-owned bytes, including the temporary NVS capacity probe.
    // This avoids exposing heap contents; the original malloc did not initialize
    // that temporary blob, which was erased immediately after the write.
    return static_cast<std::uint8_t*>(std::calloc(size,1));
}
void NvsPorts::release(std::uint8_t* bytes,bool) {std::free(bytes);}
void NvsPorts::error(unsigned line,int status,std::uint32_t old_size,std::uint32_t new_size) {
    serial_.printf("[E][MemoryHandler.h:%u] status=%d old=%u new=%u\r\n",line,status,old_size,new_size);
}
void NvsPorts::key_size(std::uint32_t size) {serial_.printf("EEPROM key_size=%u\r\n",size);}
void NvsPorts::ready() {serial_.println("EEPROM BEGIN=EEPROM finished");}
}} // namespace picostill::oem
