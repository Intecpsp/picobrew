#include "picostill/wireless_update.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace picostill::update;
struct SimulatedFlash final : Flash {
    Role running = Role::Application, selected = Role::Application;
    bool ready = true, write_ok = true, finish_ok = true;
    unsigned begins = 0, id = 24, finishes = 0, writes = 0, abandons = 0;
    std::vector<std::uint8_t> image;
    Role role() const override { return ready ? running : Role::Unavailable; }
    const char* running_sha256() const override { return "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"; }
    std::uint32_t running_size() const override { return 810080; }
    bool select_recovery() override { if (!ready) return false; selected = Role::Recovery; return true; }
    bool start(std::uint32_t) override {
        if (running != Role::Recovery || selected != Role::Recovery) return false;
        ++begins; image.clear(); return true;
    }
    void abandon() override { ++abandons; }
    std::uint32_t session_id() override { return ++id; }
    bool write(const std::uint8_t* p, std::size_t n) override {
        assert(running == Role::Recovery && selected == Role::Recovery);
        ++writes; if (!write_ok) return false;
        image.insert(image.end(), p, p+n); return true;
    }
    bool matches(std::uint32_t off, const std::uint8_t* p, std::size_t n) override {
        return off+n <= image.size() && !std::memcmp(p, image.data()+off, n);
    }
    bool finish(std::uint32_t n, const char* version, const char*) override {
        assert(running == Role::Recovery && selected == Role::Recovery && image.size() == n);
        ++finishes; if (!finish_ok || std::strcmp(version,"1.2.7")) return false;
        selected = Role::Application; return true;
    }
};
struct Requests {
    Service updater;
    bool idle = true, authenticated = true;
    std::string response;
    explicit Requests(Flash& f) : updater(f) {}
    unsigned send(const char* path, const std::string& body = "", bool ota = true) {
        char arena[PICO_HTTP_ARENA_CAPACITY];
        assert(body.size() < sizeof(arena)); std::memcpy(arena, body.c_str(), body.size()+1);
        PicoHttpRequest r = {}; r.auth_configured = true; r.authenticated = authenticated;
        r.method = body.empty() ? PICO_HTTP_GET : PICO_HTTP_POST;
        ::snprintf(r.path, sizeof(r.path), "%s%s", ota ? "/api/v1/firmware/ota" : "", path);
        r.body = arena; r.body_length = body.size(); PicoHttpResponse res = {};
        if (!updater.handle(r, res, arena, sizeof(arena), "001122334455", idle)) return 0;
        response.assign(reinterpret_cast<const char*>(res.body), res.body_length); return res.status;
    }
};
int main() {
    const std::string enter = "{\"device_uid\":\"001122334455\"}";
    auto begin = [](unsigned size, std::string version="1.2.7") {
        return "{\"device_uid\":\"001122334455\",\"model\":\"picostill\",\"sha256\":\"" + std::string(64,'a') +
            "\",\"size\":" + std::to_string(size) + ",\"version\":\"" + version + "\"}";
    };
    auto chunk = [](unsigned id, unsigned off, const std::string& data) {
        return "{\"offset\":"+std::to_string(off)+",\"session\":"+std::to_string(id)+",\"data\":\""+data+"\"}";
    };
    auto finish = [](unsigned id) { return "{\"session\":"+std::to_string(id)+"}"; };
    SimulatedFlash f; Requests app(f);
    assert(app.send("", "") == 200 && app.response.find("\"image_size\":810080")!=std::string::npos);
    assert(app.send("/begin",begin(4)) == 409 && f.begins == 0);
    app.authenticated = false; assert(app.send("/enter",enter) == 401); app.authenticated = true;
    app.idle = false; assert(app.send("/enter",enter) == 409); app.idle = true;
    f.ready = false; assert(app.send("/enter",enter) == 409); f.ready = true;
    assert(app.send("/enter", "{\"device_uid\":\"different\"}") == 400 && f.selected == Role::Application);
    assert(app.send("/enter", enter) == 202 && f.selected == Role::Recovery && app.updater.reboot_pending());
    assert(app.send("/api/v1/commands", "{}",false) == 409);
    f.running = f.selected; Requests rescue(f);
    assert(rescue.send("/api/v1/commands", "{}",false) == 409);
    assert(rescue.send("/begin", begin(SlotBytes+1)) == 400);
    assert(rescue.send("/begin", begin(5,"1.02.7")) == 400);
    auto invalid = begin(5); invalid.insert(invalid.size()-1,",\"size\":5"); assert(rescue.send("/begin",invalid)==400);
    assert(rescue.send("/begin",begin(5))==200 && f.begins==1 && f.id==25);
    assert(rescue.send("/chunk",chunk(24,0,"11"))==409);
    assert(rescue.send("/chunk",chunk(25,0,"123"))==400);
    assert(rescue.send("/chunk",chunk(25,0,"xz"))==400);
    assert(rescue.send("/chunk",chunk(25,0,std::string(2050,'a')))==400);
    assert(rescue.send("/chunk",chunk(25,1,"0a"))==409);
    assert(rescue.send("/chunk",chunk(25,0,"102030"))==200);
    assert(rescue.send("/begin",begin(5))==200 && rescue.response=="{\"session\":25,\"offset\":3}" && f.begins==1);
    assert(rescue.send("/chunk",chunk(25,0,"102030"))==200 && f.writes==1);
    assert(rescue.send("/chunk",chunk(25,0,"102031"))==409);
    assert(rescue.send("/chunk",chunk(25,2,"3040"))==409);
    assert(rescue.send("/finish",finish(25))==409 && f.finishes==0);
    // A power cut forgets the RAM handle and keeps factory selected at every byte boundary.
    for (unsigned partial = 0; partial < 5; ++partial) {
        Requests after_power_loss(f); assert(after_power_loss.send("/begin",begin(5))==200);
        auto id = f.id;
        if (partial) assert(after_power_loss.send("/chunk",chunk(id,0,std::string(partial*2,'2')))==200);
        assert(f.selected==Role::Recovery && after_power_loss.send("/finish",finish(id))==409);
    }
    Requests verifier(f);
    assert(verifier.send("/begin",begin(5))==200);
    auto id = f.id;
    f.write_ok=false; assert(verifier.send("/chunk",chunk(id,0,"aa"))==503); f.write_ok=true;
    assert(verifier.send("/chunk",chunk(id,0,"aa"))==409 && !verifier.updater.reboot_pending());
    assert(verifier.send("/begin",begin(5))==200 && f.id!=id);
    id = f.id;
    assert(verifier.send("/chunk",chunk(id,0,"1122334455"))==200);
    f.finish_ok=false; assert(verifier.send("/finish",finish(id))==422 && f.selected==Role::Recovery);
    f.finish_ok=true; assert(verifier.send("/begin",begin(1024))==200);
    assert(verifier.send("/chunk",chunk(f.id,0,std::string(2048,'a')))==200);
    assert(verifier.send("/finish",finish(f.id))==202 && verifier.response=="{\"accepted\":true,\"session\":"+std::to_string(f.id)+",\"offset\":1024}");
    assert(f.selected==Role::Application && verifier.updater.reboot_pending());
    f.running=f.selected; Requests confirmation(f);
    assert(confirmation.send("")==200 && confirmation.response.find("\"mode\":\"application\"")!=std::string::npos);
    assert(confirmation.send("/api/v1/status","",false)==0);
    puts("PicoStill wireless update authentication, retries, interrupted writes, image rejection and final selection passed");
}
