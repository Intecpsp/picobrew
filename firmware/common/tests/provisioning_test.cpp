#include "picobrew/provisioning.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
using namespace picobrew::standalone;
namespace {
struct Files {
    std::map<std::string, std::vector<std::uint8_t>> files;
    bool busy = false, fail = false, corrupt = false;
    Storage ports() {
        Storage s; s.context = this;
        s.read = [](void* p, const char* name, std::size_t offset, std::uint8_t* out, std::size_t capacity, std::size_t& bytes, std::size_t& total) {
            Files& f = *static_cast<Files*>(p); bytes = total = 0;
            const auto it = f.files.find(name); if (it == f.files.end()) return StorageResult::Missing;
            total = it->second.size(); if (offset > total) return StorageResult::Error;
            bytes = std::min(capacity, total - offset); if (bytes) std::memcpy(out, it->second.data() + offset, bytes); return StorageResult::Ok;
        };
        s.write_atomic = [](void* p, const char* name, const std::uint8_t* bytes, std::size_t size) {
            Files& f = *static_cast<Files*>(p); if (f.fail) return StorageResult::Error;
            f.files[name] = std::vector<std::uint8_t>(bytes, bytes + size);
            if (f.corrupt && size) f.files[name].back() ^= 1;
            return StorageResult::Ok;
        };
        return s;
    }
    static bool idle(void* p) { return !static_cast<Files*>(p)->busy; }
};
}
int main() {
    Files files; Provisioning provision(files.ports(), &files, Files::idle);
    assert(provision.load().ok()); assert(!provision.key_configured()); assert(!provision.network_configured());
    const std::string key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    const std::string input = "key " + key + "\r\n"; Result result; unsigned completed = 0;
    for (char c : input) if (provision.feed(c, result)) ++completed;
    assert(completed == 1 && result.ok()); assert(std::string(result.message).find(key) == std::string::npos);
    assert(provision.key_configured() && key == provision.key()); assert(provision.take_key_changed()); assert(!provision.take_key_changed());
    const char credentials[] = "wifi My access point\tpassword value";
    assert(provision.execute(credentials, sizeof(credentials) - 1).ok());
    assert(std::string(provision.network().ssid) == "My access point"); assert(provision.take_network_changed());
    assert(std::string(provision.network().password) == "password value");
    Provisioning reboot(files.ports(), &files, Files::idle); assert(reboot.load().ok());
    assert(key == reboot.key()); assert(std::string(reboot.network().ssid) == "My access point");
    NetworkCredentials imported; std::strcpy(imported.ssid, "OEM"); std::strcpy(imported.password, "old password");
    assert(reboot.import_network(imported).status == 409);
    files.busy = true; assert(reboot.execute(input.data(), input.size() - 2).status == 409); files.busy = false;
    const char* bad[] = {"key ABCD", "wifi no-tab password", "wifi name\tshort", "wifi \tpassword", "status extra", "key "};
    for (const char* line : bad) assert(reboot.execute(line, std::strlen(line)).status == 400);
    files.fail = true; assert(reboot.execute("wifi New\tpassword", 17).status == 503); files.fail = false;
    assert(std::string(reboot.network().ssid) == "My access point");
    files.corrupt = true; assert(reboot.execute(input.data(), input.size() - 2).status == 503); files.corrupt = false;
    Provisioning corrupt(files.ports(), &files, Files::idle); assert(corrupt.load().status == 503); assert(!corrupt.key_configured());
    for (unsigned i = 0; i < 200; ++i) assert(!reboot.feed('a', result));
    assert(reboot.feed('\n', result) && result.status == 400); assert(!reboot.feed('\n', result));
    const std::string repair = "key " + key;
    assert(reboot.execute(repair.c_str(), repair.size()).ok());
    Files blank; Provisioning fresh(blank.ports(), &blank, Files::idle); assert(fresh.load().ok());
    assert(fresh.import_network(imported).ok()); assert(std::string(fresh.network().ssid) == "OEM");
    blank.files["network.a"].back() ^= 1;
    Provisioning unreadable(blank.ports(), &blank, Files::idle); assert(unreadable.load().status == 503);
    assert(unreadable.import_network(imported).status == 409);
    std::puts("Provisioning parser/storage tests passed: verified key/network writes, no echo, busy guard, corrupt state preservation and bounded input");
}
