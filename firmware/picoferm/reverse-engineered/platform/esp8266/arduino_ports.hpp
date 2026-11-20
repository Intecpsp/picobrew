#pragma once
#include "picoferm/core.hpp"
#include <EEPROM.h>

namespace picoferm { namespace oem {
// Concrete SDK bindings for the recovered modules. Caller owns the EEPROM
// flash-sector/linker choice and supplies the original configured allocation.
class ArduinoPorts final : public Ports {
public:
    explicit ArduinoPorts(EEPROMClass& eeprom) noexcept : eeprom_(eeprom) {}
    bool begin_storage(std::size_t allocation);
    void load(PersistentData& data);
    void initialize_button();
    bool last_commit_succeeded() const noexcept { return commit_succeeded_; }
    void yield() override;
    void text(const char* scope,const char* key,const char* value) override;
    void integer(const char* scope,const char* key,std::uint32_t value) override;
    void number(const char* scope,const char* key,float value) override;
    void boolean(const char* scope,const char* key,bool value) override;
    void digital_write(unsigned pin,bool value) override;
    std::uint32_t digital_read(unsigned pin) override;
    void persist(const std::uint8_t* bytes,std::size_t size) override;
private:
    EEPROMClass& eeprom_;
    bool commit_succeeded_ = false;
};
}} // namespace picoferm::oem
