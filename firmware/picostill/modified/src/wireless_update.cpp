#include "picostill/wireless_update.hpp"
#include "../../../common/src/json.hpp"
#include <algorithm>
#include <cstring>

namespace picostill { namespace update {
namespace {
using picobrew::standalone::detail::Json;
constexpr char Path[] = "/api/v1/firmware/ota";
enum Field : unsigned { Device = 1, Model = 2, VersionName = 4, Digest = 8, Size = 16,
                        Session = 32, Offset = 64, Data = 128 };
struct Fields {
    char uid[32] = {}, model[16] = {}, version[32] = {}, digest[65] = {};
    std::uint32_t size = 0, session = 0, offset = 0;
    std::uint8_t data[ChunkBytes];
    std::size_t length = 0;
};
bool number(Json& in, std::uint32_t low, std::uint32_t high, std::uint32_t& result) {
    std::int64_t value = 0;
    if (!in.integer(low, high, value)) return false;
    result = static_cast<std::uint32_t>(value);
    return true;
}
bool payload(Json& in, Fields& body) {
    if (!in.token('"')) return false;
    while (in.end - in.p >= 2 && *in.p != '"') {
        const int a = Json::hex(*in.p++), b = Json::hex(*in.p++);
        if (a < 0 || b < 0 || body.length == ChunkBytes) return false;
        body.data[body.length++] = static_cast<std::uint8_t>((a << 4) | b);
    }
    return body.length && in.p < in.end && *in.p++ == '"';
}
bool parse(const PicoHttpRequest& request, unsigned required, Fields& body) {
    Json in(request.body, request.body_length);
    if (!in.token('{')) return false;
    unsigned seen = 0;
    while (!in.peek('}')) {
        char key[40]; if (!in.key(key)) return false;
        unsigned field;
        if (!std::strcmp(key, "device_uid")) { field = Device; if (!in.string(body.uid, sizeof(body.uid))) return false; }
        else if (!std::strcmp(key, "model")) { field = Model; if (!in.string(body.model, sizeof(body.model))) return false; }
        else if (!std::strcmp(key, "version")) { field = VersionName; if (!in.string(body.version, sizeof(body.version))) return false; }
        else if (!std::strcmp(key, "sha256")) { field = Digest; if (!in.string(body.digest, sizeof(body.digest))) return false; }
        else if (!std::strcmp(key, "size")) { field = Size; if (!number(in, 1, SlotBytes, body.size)) return false; }
        else if (!std::strcmp(key, "session")) { field = Session; if (!number(in, 1, UINT32_MAX, body.session)) return false; }
        else if (!std::strcmp(key, "offset")) { field = Offset; if (!number(in, 0, SlotBytes, body.offset)) return false; }
        else if (!std::strcmp(key, "data")) { field = Data; if (!payload(in, body)) return false; }
        else return false;
        if ((seen & field) || !(required & field)) return false;
        seen |= field;
        if (in.peek('}')) break;
        if (!in.token(',') || in.peek('}')) return false;
    }
    return in.token('}') && in.done() && seen == required;
}
bool version_ok(const char* s) {
    for (unsigned part = 0; part < 3; ++part) {
        const char* start = s;
        while (*s >= '0' && *s <= '9') ++s;
        if (s == start || (s - start > 1 && *start == '0')) return false;
        if (part < 2 && *s++ != '.') return false;
    }
    return !*s;
}
bool digest_ok(const char* s) { return std::strlen(s) == 64 && std::strspn(s, "0123456789abcdef") == 64; }
bool reply(PicoHttpResponse& response, unsigned status, const char* json) {
    response = {status, "application/json", reinterpret_cast<const std::uint8_t*>(json), std::strlen(json), nullptr};
    return true;
}
}
void Service::clear() { session_ = offset_ = size_ = 0; version_[0] = digest_[0] = 0; }
bool Service::handle(const PicoHttpRequest& req, PicoHttpResponse& res, char* arena, std::size_t capacity,
                     const char* uid, bool idle) {
    const Role role = flash_.role();
    if (std::strncmp(req.path, Path, sizeof(Path) - 1) ||
        (req.path[sizeof(Path)-1] && req.path[sizeof(Path)-1] != '/')) {
        if (role == Role::Recovery || role == Role::InvalidRecovery || reboot_)
            return reply(res, 409, "{\"error\":\"recovery_mode\"}");
        return false;
    }
    if (!req.auth_configured || !req.authenticated) return reply(res, 401, "{\"error\":\"unauthorized\"}");
    if (!uid || std::strlen(uid) != 12 || std::strspn(uid, "0123456789abcdef") != 12 ||
        role == Role::Unavailable || role == Role::InvalidRecovery)
        return reply(res, 409, "{\"error\":\"recovery_unavailable\"}");
    const char* action = req.path + sizeof(Path) - 1;
    if (!*action && req.method == PICO_HTTP_GET) {
        int n = ::snprintf(arena, capacity,
              "{\"protocol\":\"%s\",\"model\":\"picostill\",\"device_uid\":\"%s\",\"mode\":\"%s\",\"version\":\"%s\","
              "\"image_sha256\":\"%s\",\"image_size\":%u,\"max_bytes\":%u,\"session\":%u,\"offset\":%u,\"size\":%u,\"sha256\":\"%s\",\"target_version\":\"%s\"}",
              Protocol, uid, role == Role::Recovery ? "recovery" : "application", Version,
              flash_.running_sha256(), unsigned(flash_.running_size()), unsigned(SlotBytes), unsigned(session_), unsigned(offset_), unsigned(size_), digest_, version_);
        return n >= 0 && static_cast<std::size_t>(n) < capacity ? reply(res, 200, arena) : reply(res, 500, "{}");
    }
    if (req.method != PICO_HTTP_POST) return reply(res, 405, "{\"error\":\"method\"}");
    if (reboot_) return reply(res, 409, "{\"error\":\"restarting\"}");
    Fields b;
    if (!std::strcmp(action, "/enter")) {
        if (!parse(req, Device, b) || std::strcmp(uid, b.uid)) return reply(res, 400, "{\"error\":\"device_uid\"}");
        if (role != Role::Application || !idle) return reply(res, 409, "{\"error\":\"not_idle_application\"}");
        if (!flash_.select_recovery()) return reply(res, 503, "{\"error\":\"recovery_unavailable\"}");
        reboot_ = true;
        return reply(res, 202, "{\"accepted\":true}");
    }
    if (role != Role::Recovery) return reply(res, 409, "{\"error\":\"requires_recovery\"}");
    if (!std::strcmp(action, "/begin")) {
        if (!parse(req, Device|Model|VersionName|Digest|Size, b) || std::strcmp(uid, b.uid) ||
            std::strcmp(b.model, "picostill") || !version_ok(b.version) || !digest_ok(b.digest))
            return reply(res, 400, "{\"error\":\"invalid_package\"}");
        if (!session_ || size_ != b.size || std::strcmp(version_, b.version) || std::strcmp(digest_, b.digest)) {
            if (session_) flash_.abandon();
            clear();
            if (!flash_.start(b.size)) return reply(res, 503, "{\"error\":\"flash_unavailable\"}");
            session_ = flash_.session_id();
            if (!session_) { flash_.abandon(); return reply(res, 503, "{\"error\":\"session_unavailable\"}"); }
            size_ = b.size;
            std::strcpy(version_, b.version); std::strcpy(digest_, b.digest);
        }
    } else if (!std::strcmp(action, "/chunk")) {
        if (!parse(req, Session|Offset|Data, b)) return reply(res, 400, "{\"error\":\"invalid_chunk\"}");
        if (b.session != session_ || b.offset > offset_ || b.offset > size_ || b.length > size_ - b.offset)
            return reply(res, 409, "{\"error\":\"wrong_offset_or_session\"}");
        if (b.offset < offset_) {
            if (b.length > offset_ - b.offset || !flash_.matches(b.offset, b.data, b.length))
                return reply(res, 409, "{\"error\":\"conflicting_chunk\"}");
        } else {
            if (!flash_.write(b.data, b.length)) {
                flash_.abandon(); clear(); return reply(res, 503, "{\"error\":\"write_failed\"}");
            }
            offset_ += b.length;
        }
    } else if (!std::strcmp(action, "/finish")) {
        if (!parse(req, Session, b)) return reply(res, 400, "{\"error\":\"invalid_session\"}");
        if (b.session != session_ || offset_ != size_) return reply(res, 409, "{\"error\":\"incomplete\"}");
        if (!flash_.finish(size_, version_, digest_)) {
            clear(); return reply(res, 422, "{\"error\":\"image_verification_failed\"}");
        }
        reboot_ = true;
        int n = ::snprintf(arena, capacity, "{\"accepted\":true,\"session\":%u,\"offset\":%u}",
                           unsigned(session_), unsigned(offset_));
        return n >= 0 && static_cast<std::size_t>(n) < capacity ? reply(res, 202, arena) : reply(res, 500, "{}");
    } else return reply(res, 404, "{\"error\":\"not_found\"}");
    int n = ::snprintf(arena, capacity, "{\"session\":%u,\"offset\":%u}", unsigned(session_), unsigned(offset_));
    return n >= 0 && static_cast<std::size_t>(n) < capacity ? reply(res, 200, arena) : reply(res, 500, "{}");
}
}}
