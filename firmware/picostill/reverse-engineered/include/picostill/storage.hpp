// Original MemoryHandler/EepromData behavior, independent of Arduino EEPROM.
#pragma once
#include "picostill/application.hpp"
#include <cstddef>

namespace picostill { namespace oem {
struct StoragePorts {
    virtual ~StoragePorts() = default;
    virtual int open(const char* name,std::uint32_t& handle) = 0;
    virtual int get_blob(std::uint32_t handle,const char* key,void* bytes,std::size_t& size) = 0;
    virtual int set_blob(std::uint32_t handle,const char* key,const void* bytes,std::size_t size) = 0;
    virtual int erase_key(std::uint32_t handle,const char* key) = 0;
    virtual int commit(std::uint32_t handle) = 0;
    virtual std::uint8_t* allocate(std::size_t size) = 0;
    virtual void release(std::uint8_t* bytes,bool array_delete) = 0;
    virtual void error(unsigned line,int status=0,std::uint32_t old_size=0,std::uint32_t new_size=0) = 0;
    virtual void key_size(std::uint32_t size) = 0;
    virtual void ready() = 0;
};
class CredentialStorage {
public:
    CredentialStorage(StoragePorts& ports,Diagnostics& diagnostics) noexcept:ports_(ports),diagnostics_(diagnostics) {}
    ~CredentialStorage() { if(buffer) ports_.release(buffer,false); }
    CredentialStorage(const CredentialStorage&) = delete;
    CredentialStorage& operator=(const CredentialStorage&) = delete;
    std::uint32_t requested_size = 512,handle = 0;
    std::uint8_t* buffer = nullptr;
    std::size_t size = 0;
    bool dirty = false;
    const char* key = "eeprom";
    bool begin(std::uint32_t requested);
    void initialize();
    void load(EepromData& destination);
    void save(const EepromData& source);
private:
    StoragePorts& ports_;
    Diagnostics& diagnostics_;
};
}} // namespace picostill::oem
