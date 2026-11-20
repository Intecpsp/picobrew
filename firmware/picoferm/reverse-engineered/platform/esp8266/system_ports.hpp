#pragma once
#include "picoferm/application.hpp"
#include <EEPROM.h>
namespace picoferm { namespace oem {
class ArduinoSystemPorts final:public SystemPorts {
public:
    explicit ArduinoSystemPorts(EEPROMClass& storage):storage_(storage){}
    void serial_begin(unsigned baud,unsigned configuration,unsigned mode,unsigned tx_pin) override;
    void serial_line(const char* value) override;void mac_address(std::uint8_t* bytes) override;
    void storage_begin(std::size_t size) override;std::size_t storage_size() override;
    void storage_read(std::uint8_t* bytes,std::size_t size) override;
    void remove_os_wifi_data() override;void deep_sleep(std::uint64_t microseconds,unsigned radio_mode) override;void restart() override;
    void update_reboot(bool enabled) override;int update(const char* url,const char* current_version) override;
    std::string update_error() override;
private:EEPROMClass& storage_;
};
}}
