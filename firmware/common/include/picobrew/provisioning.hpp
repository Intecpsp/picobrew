// Physical-console provisioning. No UART ownership, network listener or echo.
#pragma once
#include "picobrew/device_services.hpp"

namespace picobrew { namespace standalone {
struct NetworkCredentials {
    char ssid[33] = {};
    char password[65] = {};
};
class Provisioning {
public:
    Provisioning(Storage storage, void* context, bool (*idle)(void*)) noexcept
        : storage_(storage), context_(context), idle_(idle) {}
    Result load();
    // Import a checked, read-only OEM record only when the modified record is
    // definitely missing. Corrupt/unreadable modified state is never replaced.
    Result import_network(const NetworkCredentials& credentials);
    const NetworkCredentials& network() const noexcept { return network_; }
    const char* key() const noexcept { return key_; }
    bool network_configured() const noexcept { return network_.ssid[0] != 0; }
    bool key_configured() const noexcept { return key_[0] != 0; }
    bool take_network_changed() noexcept;
    bool take_key_changed() noexcept;
    // Feed at most the platform's bounded serial budget each poll. A complete
    // nonempty line returns true and an input-free, safe-to-print Result.
    // Commands: key <64 lowercase hex>; wifi <SSID><TAB><password>; status.
    bool feed(char byte, Result& result);
    Result execute(const char* line, std::size_t length);
private:
    Storage storage_;
    void* context_;
    bool (*idle_)(void*);
    NetworkCredentials network_;
    char key_[65] = {}, line_[160] = {};
    std::size_t line_length_ = 0;
    std::uint32_t network_generation_ = 0;
    bool loaded_ = false, network_exists_ = false, discarded_ = false;
    bool network_changed_ = false, key_changed_ = false;
    Result save_network(const NetworkCredentials&);
    Result save_key(const char* value);
};
}}
