#include "engine_internal.hpp"

namespace picobrew { namespace standalone {
using namespace detail;
namespace {
constexpr std::size_t TxBytes = 4608;
const char* const RequestHeader =
    "POST /api/v1/telemetry HTTP/1.0\r\nHost: %s:%u\r\nContent-Type: application/json\r\nContent-Length: %u\r\nConnection: close\r\n\r\n";
void increment(std::uint32_t& counter, std::uint32_t count = 1) { counter = UINT32_MAX - counter < count ? UINT32_MAX : counter + count; }
}
void Engine::Impl::tx_close() {
    if (tx_open && owner.telemetry_.close) owner.telemetry_.close(owner.telemetry_.context);
    tx_open = false; tx_offset = tx_length = 0; tx_received = 0;
    std::free(tx); tx = nullptr;
}
void Engine::Impl::tx_fail(const char* reason) {
    tx_close(); tx_error = reason; tx_state = TxState::Backoff; tx_backoff = now();
    increment(tx_failed); increment(tx_dropped);
}
bool Engine::Impl::tx_frame() {
    // Count exactly the same immutable snapshot before allocating. A small
    // accessory frame need not retain the maximum brewer-sized buffer, and
    // tx_close releases it after success, failure, disable or rebinding.
    Writer measured;
    measured.text("{\"schema_version\":1,\"firmware_version\":"); measured.quote(identity.firmware_version); measured.text(",\"status\":");
    if (status_json(measured, false) != 200) return false;
    measured.text("}"); if (!measured.ok || measured.length >= TxBytes) return false;
    const int header = ::snprintf(nullptr, 0, RequestHeader, telemetry.host, telemetry.port, static_cast<unsigned>(measured.length));
    if (header <= 0 || static_cast<std::size_t>(header) + measured.length >= TxBytes) return false;
    tx = static_cast<std::uint8_t*>(std::malloc(header + measured.length + 1));
    if (!tx) return false;
    if (::snprintf(reinterpret_cast<char*>(tx), header + 1, RequestHeader,
                   telemetry.host, telemetry.port, static_cast<unsigned>(measured.length)) != header) return false;
    Writer writer(reinterpret_cast<char*>(tx + header), measured.length + 1);
    writer.text("{\"schema_version\":1,\"firmware_version\":"); writer.quote(identity.firmware_version); writer.text(",\"status\":");
    if (status_json(writer, false) != 200) return false;
    writer.text("}"); if (!writer.ok || writer.length != measured.length) return false;
    tx_length = header + writer.length;
    tx_offset = 0; tx_received = 0; return true;
}
void Engine::Impl::tx_poll() {
    if (!telemetry.enabled) return;
    const std::uint32_t time = now(), periods = (time - tx_scheduled) / SamplePeriod;
    const bool due = periods != 0;
    if (due) tx_scheduled += periods * SamplePeriod;
    const TelemetryIo& io = owner.telemetry_;
    if (!io.begin || !io.poll_connect || !io.send || !io.receive || !io.close || !snapshot_valid) {
        tx_close(); tx_state = TxState::Offline; tx_resolved = false; if (due) increment(tx_dropped, periods); return;
    }
    if (tx_state == TxState::Backoff) {
        if (time - tx_backoff < 30000) { if (due) increment(tx_dropped, periods); return; }
        tx_state = TxState::Ready;
    }
    if (tx_state == TxState::Offline || tx_state == TxState::Disabled) tx_state = TxState::Ready;
    if (tx_state == TxState::Ready) {
        if (!due) return;
        if (periods > 1) increment(tx_dropped, periods - 1);
        if (!tx_frame()) { tx_fail("capacity"); return; }
        tx_open = true; tx_resolved = false; tx_started = time; tx_http = 0;
        if (!io.begin(io.context, telemetry.host, telemetry.port)) { tx_fail("connect"); return; }
        tx_state = TxState::Connecting;
    } else if (due) increment(tx_dropped, periods);
    if (time - tx_started >= 5000) { tx_fail("timeout"); return; }
    if (tx_state == TxState::Connecting) {
        const int state = io.poll_connect(io.context);
        if (state < 0 || state > 1) { tx_fail("connect"); return; }
        if (!state) return;
        tx_resolved = true; tx_state = TxState::Sending;
    }
    std::size_t budget = PICO_HTTP_IO_BUDGET; unsigned calls = 0;
    while (tx_state == TxState::Sending && budget && calls++ < PICO_HTTP_IO_CALL_BUDGET) {
        const std::size_t remaining = tx_length - tx_offset, count = remaining < budget ? remaining : budget;
        const int sent = io.send(io.context, tx + tx_offset, count);
        if (sent == PICO_HTTP_IO_AGAIN) return;
        if (sent <= 0 || static_cast<std::size_t>(sent) > count) { tx_fail("send"); return; }
        tx_offset += sent; budget -= sent;
        if (tx_offset == tx_length) tx_state = TxState::Receiving;
    }
    if (tx_state != TxState::Receiving || !budget || calls >= PICO_HTTP_IO_CALL_BUDGET) return;
    const unsigned count = sizeof(tx_response) - tx_received;
    const int received = io.receive(io.context, reinterpret_cast<std::uint8_t*>(tx_response) + tx_received, count);
    if (received == PICO_HTTP_IO_AGAIN) return;
    if (received <= 0 || static_cast<unsigned>(received) > count) { tx_fail("receive"); return; }
    tx_received += received;
    if (tx_received < sizeof(tx_response)) return;
    if (std::memcmp(tx_response, "HTTP/1.", 7) || (tx_response[7] != '0' && tx_response[7] != '1') || tx_response[8] != ' ' ||
        tx_response[9] < '1' || tx_response[9] > '5' || tx_response[10] < '0' || tx_response[10] > '9' ||
        tx_response[11] < '0' || tx_response[11] > '9' || (tx_response[12] != ' ' && tx_response[12] != '\r')) { tx_fail("http"); return; }
    tx_http = (tx_response[9] - '0') * 100 + (tx_response[10] - '0') * 10 + tx_response[11] - '0';
    if (tx_http < 200 || tx_http >= 300) { tx_fail("http"); return; }
    tx_close(); increment(tx_sent); tx_success = time; tx_error = "none"; tx_state = TxState::Ready;
}
unsigned Engine::Impl::telemetry_api(const PicoHttpRequest& request, Writer& writer) {
    if (request.method != PICO_HTTP_GET && request.method != PICO_HTTP_PUT)
        return error(writer, Result(405, "method_not_allowed", "Use GET or PUT for telemetry settings"));
    if (request.method == PICO_HTTP_PUT) {
        if (!idle()) return error(writer, Result(409, "busy", "Change forwarding settings only while the appliance is idle"));
        if (!persistent) return error(writer, Result(503, "storage_io", "Persistent state is unavailable"));
        Json json(request.body, request.body_length); TelemetryConfig changed; unsigned seen = 0;
        bool valid = json.token('{') && !json.peek('}');
        while (valid) {
            char key[40]; unsigned field = 0; std::int64_t value = 0;
            if (!json.key(key)) { valid = false; break; }
            if (!std::strcmp(key, "enabled")) { field = 1; valid = json.boolean(changed.enabled); }
            else if (!std::strcmp(key, "host")) { field = 2; valid = json.string(changed.host, sizeof(changed.host)); }
            else if (!std::strcmp(key, "port")) { field = 4; valid = json.integer(1, 65535, value); changed.port = value; }
            else valid = false;
            if (seen & field) valid = false;
            seen |= field; if (!valid || !json.next('}')) break;
        }
        if (!valid || seen != 7 || !json.token('}') || !json.done() || !telemetry_valid(changed))
            return error(writer, Result(400, "invalid_telemetry", "Use enabled, an IPv4 address or hostname, and port 1-65535"));
        const Result saved = save_config(settings, changed);
        if (!saved.ok()) { tx_storage_error = true; tx_error = "config"; return error(writer, saved); }
        tx_close(); tx_storage_error = false; tx_error = "none"; tx_resolved = false; tx_http = 0;
        tx_state = changed.enabled ? TxState::Ready : TxState::Disabled; tx_scheduled = now() - SamplePeriod;
        if (!changed.enabled) { std::free(tx); tx = nullptr; }
        owner.event("telemetry", changed.enabled ? "enabled" : "disabled");
    }
    static const char* const states[] = {"disabled", "offline", "ready", "connecting", "sending", "receiving", "backoff"};
    writer.text("{\"enabled\":"); writer.boolean(telemetry.enabled); writer.text(",\"host\":"); writer.quote(telemetry.host);
    writer.format(",\"port\":%u,\"interval_ms\":%u,\"path\":\"/api/v1/telemetry\",\"status\":{\"state\":", telemetry.port, SamplePeriod);
    writer.quote(states[static_cast<unsigned>(tx_state)]);
    writer.format(",\"resolved\":%s,\"sent\":%lu,\"failed\":%lu,\"dropped\":%lu,\"last_success_ms\":%lu,\"last_http_status\":%u,\"storage_error\":%s,\"last_error\":",
        tx_resolved ? "true" : "false", static_cast<unsigned long>(tx_sent), static_cast<unsigned long>(tx_failed), static_cast<unsigned long>(tx_dropped),
        static_cast<unsigned long>(tx_success), tx_http, tx_storage_error ? "true" : "false");
    writer.quote(tx_error); writer.text("}}"); return 200;
}
}}
