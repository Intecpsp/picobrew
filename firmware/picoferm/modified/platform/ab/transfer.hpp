// PicoFerm Wi-Fi update receiver. A checksum-protected permanent bootstrap chooses
// between separate images. The receiver can only erase the other image's sectors.
#pragma once
#include "boot.h"
#include "picobrew/http_server.h"
#include <cstdint>
namespace picoferm {
struct UpdateFlash {
    FermBootIO io;
    bool (*erase)(void*, unsigned sector);
    uint32_t (*random)(void*);
};
class Update {
public:
    explicit Update(UpdateFlash flash) : flash_(flash) {}
    bool attach(unsigned running, const char* uid, const char* version, bool storage_ready);
    bool handle(const PicoHttpRequest&, PicoHttpResponse&, char* output, size_t capacity, bool idle);
    bool locked() const { return active_ || trial_; }
    void cancel();
    bool trial() const { return trial_; }
    bool reboot() const { return reboot_; }
private:
    bool digest(uint32_t address, uint32_t size, uint8_t hash[32], uint32_t* crc);
    bool begin(const char* version, const char* hash, uint32_t size);
    bool chunk(uint32_t offset, const char* hex, uint32_t length);
    bool finish();
    UpdateFlash flash_;
    unsigned running_ = 0;
    FermBootRecord current_{0,0,0,0,0,0,0};
    char uid_[13]{}, version_[19]{}, target_[19]{}, hash_[65]{}, session_[17]{};
    uint8_t image_digest_[32]{};
    uint32_t written_ = 0, size_ = 0;
    bool available_ = false, active_ = false, reboot_ = false, trial_ = false;
};
}
