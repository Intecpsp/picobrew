#include "picobrew/standalone_engine.hpp"
#include "../src/engine_internal.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>

using namespace picobrew::standalone;
namespace {
struct World {
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<PicoHttpServer> http;
    int pairing_mode = -1;
    Snapshot snapshot;
    Settings settings;
    std::string command, state = "idle", output, sent, received = "HTTP/1.0 204 No Content\r\n\r\n", read_error;
    unsigned model = 0, status = 0, writes = 0, fail_write = 0, torn_write = 0, start_count = 0, stop_count = 0, token = 0, resumes = 0, discards = 0;
    unsigned tx_begin = 0, tx_close = 0, tx_calls = 0, tx_max_send = 0, tx_bytes_poll = 0, tx_max_poll = 0;
    std::uint32_t time = 0, revision = 42, history_interval = 15000;
    std::size_t received_at = 0;
    int tx_connect = 1, tx_send_limit = 73, tx_receive_limit = 3;
    bool prepare = true, settings_fail = false, display_changes = false;
    static World& self(void* p) { return *static_cast<World*>(p); }
    explicit World(unsigned kind) : model(kind) {
        snapshot.ready = true;
        if (!model) {
            snapshot.wort_f.valid = snapshot.thermoblock_f.valid = snapshot.target_f.valid = true;
            snapshot.wort_f.value = 152.2; snapshot.thermoblock_f.value = 179.1; snapshot.target_f.value = 154;
        } else {
            const char* still_ids[] = {"inlet", "outlet", "pot", "ambient", "pressure"};
            const char* still_labels[] = {"Inlet", "Outlet", "Pot", "Ambient", "Pressure"};
            const char* ferm_ids[] = {"temperature", "pressure_native", "battery"};
            const char* ferm_labels[] = {"Temperature", "Pressure native", "Battery"};
            snapshot.sensor_count = model == 1 ? 3 : 5;
            for (std::size_t i = 0; i < snapshot.sensor_count; ++i) {
                Sensor& sensor = snapshot.sensors[i]; sensor.id = model == 1 ? ferm_ids[i] : still_ids[i];
                sensor.label = model == 1 ? ferm_labels[i] : still_labels[i]; sensor.reading.valid = true;
                sensor.reading.value = 60.5 + i; sensor.unit = Unit::Fahrenheit; sensor.kind = SensorKind::Temperature;
            }
            if (model == 1) {
                snapshot.sensors[1].unit = Unit::Raw; snapshot.sensors[1].kind = SensorKind::Pressure;
                snapshot.sensors[2].unit = Unit::Volt; snapshot.sensors[2].kind = SensorKind::Battery;
                snapshot.sensors[2].reading.value = 3.61; snapshot.battery_v = snapshot.sensors[2].reading;
            } else {
                snapshot.sensors[4].unit = Unit::Kilopascal; snapshot.sensors[4].kind = SensorKind::Pressure;
                snapshot.pressure_kpa = snapshot.sensors[4].reading;
                snapshot.actuator_count = 3;
                const char* ids[] = {"pump", "fan1", "fan2"}; const char* labels[] = {"Pump", "Fan 1", "Fan 2"};
                for (unsigned i = 0; i < 3; ++i) {
                    Actuator& a = snapshot.actuators[i]; a.id = ids[i]; a.label = labels[i];
                    a.kind = i ? ActuatorKind::Fan : ActuatorKind::Pump; a.valid = true;
                    a.supports_level = a.supports_rpm = i != 0;
                    a.level.valid = true; a.level.value = i * 30; a.rpm.valid = i != 0; a.rpm.value = i * 550;
                }
            }
            snapshot.temperature_f = snapshot.sensors[0].reading;
        }
        reboot();
    }
    DeviceServices device() {
        DeviceServices ports; ports.context = this;
        ports.identity = [](void* p) { World& w = self(p); Identity id;
            id.model = w.model == 1 ? "picoferm" : w.model == 2 ? "picostill" : "z-series";
            id.model_name = w.model == 1 ? "PicoFerm" : w.model == 2 ? "PicoStill" : "Pico Z";
            id.model_family = id.model; id.firmware_version = "1.1.0"; id.device_uid = "000011112222"; return id; };
        ports.capabilities = [](void* p) { World& w = self(p); Capabilities c;
            static const CommandDescriptor brewer[] = {{"brew", RecipeId, RecipeId}, {"stop", 0, 0}, {"clear-stop", 0, 0}, {"encoder", Delta | Click, 0},
                {"resume-interrupted", Confirm, Confirm}, {"discard-interrupted", 0, 0}};
            static const CommandDescriptor ferm[] = {{"start", 0, 0}, {"stop", 0, 0}, {"calibrate", Confirm, Confirm}, {"sample", 0, 0}, {"set-sampling-interval", Value, Value}};
            static const CommandDescriptor still[] = {{"start", 0, 0}, {"stop", 0, 0}, {"essential-oils", 0, 0}, {"clean", 0, 0}, {"set-pump", Enabled, Enabled}, {"set-fan", Channel | Level | Enabled, Channel | Level}};
            c.flags = w.model == 1 ? Temperature | Pressure | Battery | Fermentation | Calibration :
                w.model == 2 ? Temperature | Pressure | Pump | Fans : Recipes | Brewing | Heating | Pump | Temperature | FrontPanel;
            c.settings = TemperatureUnit | Language | (w.model ? 0u : DetailedStatus);
            c.commands = w.model == 1 ? ferm : w.model == 2 ? still : brewer;
            c.command_count = w.model == 1 ? 5 : w.model == 2 ? 6 : 6;
            c.zero_minute_steps = true; return c; };
        ports.snapshot = [](void* p, Snapshot& s) { World& w = self(p); w.snapshot.uptime_ms = w.time;
            w.snapshot.command = w.command.c_str(); w.snapshot.state = w.state.c_str(); s = w.snapshot; return Result(); };
        ports.idle = [](void* p) { return !self(p).snapshot.busy; };
        ports.history_interval_ms = [](void* p) { return self(p).history_interval; };
        ports.read_settings = [](void* p, Settings& s) { s = self(p).settings; return Result(); };
        ports.validate_settings = [](void*, const Settings&, std::uint32_t) { return Result(); };
        ports.apply_settings = [](void* p, const Settings& s, std::uint32_t) { World& w = self(p); w.settings = s;
            return w.settings_fail && s.temperature_unit == 'C' ? Result(503, "device_failure", "Injected apply failure") : Result(); };
        ports.validate_recipe = [](void*, const Recipe& recipe) { return recipe.steps[0].location == 6 ? Result(400, "native_recipe", "Unsupported route") : Result(); };
        ports.execute = [](void* p, const Command& c) { World& w = self(p);
            if (!std::strcmp(c.name, "resume-interrupted")) {
                if (!w.snapshot.recovery.available || !w.snapshot.recovery.valid) return Result(409, "no_recovery", "No valid checkpoint");
                ++w.resumes; w.snapshot.recovery.available = false; return Result(202);
            }
            if (!std::strcmp(c.name, "discard-interrupted")) {
                ++w.discards; w.snapshot.recovery.available = false; w.snapshot.recovery.valid = true; return Result(202);
            }
            if (!std::strcmp(c.name, "stop")) { ++w.stop_count; w.snapshot.busy = w.snapshot.recording = false; w.snapshot.stopped = true; w.state = "stopped"; return Result(202); }
            if (!std::strcmp(c.name, "clear-stop")) { w.snapshot.stopped = false; w.state = "idle"; return Result(202); }
            if (!std::strcmp(c.name, "brew") || !std::strcmp(c.name, "start") || !std::strcmp(c.name, "clean") || !std::strcmp(c.name, "essential-oils")) {
                if (w.snapshot.busy) return Result(409, "busy", "Already running");
                const std::uint32_t token = ++w.token;
                if (w.prepare) { const Result result = w.engine->prepare_session(c.name, c.recipe ? c.recipe->name : "", token); if (!result.ok()) return result; }
                ++w.start_count; w.command = c.name; w.state = "running"; w.snapshot.recording = w.snapshot.busy = true;
                w.snapshot.stopped = false; w.snapshot.session_token = token;
                if (c.recipe) std::snprintf(w.snapshot.recipe_name, sizeof(w.snapshot.recipe_name), "%s", c.recipe->name);
                return Result(202);
            }
            if (!std::strcmp(c.name, "set-fan") && (c.channel < 1 || c.channel > 2)) return Result(400, "invalid_channel", "Use fan 1 or 2");
            if (!std::strcmp(c.name, "set-sampling-interval") && (c.value != std::floor(c.value) || c.value < 60 || c.value > 86400)) return Result(400, "invalid_interval", "Use integer seconds");
            return Result(202); };
        ports.display = [](void* p, Display& d) { d.width = 128; d.height = 64; d.format = DisplayFormat::RowGray4Msb; d.total_bytes = 4096; d.revision = self(p).revision; return Result(); };
        ports.read_display = [](void* p, std::uint32_t revision, std::size_t offset, std::uint8_t* data, std::size_t n) {
            World& w = self(p); if (w.display_changes) ++w.revision;
            if (revision != w.revision) return Result(409, "display_changed", "Changed");
            for (std::size_t i = 0; i < n; ++i) data[i] = static_cast<std::uint8_t>(offset + i);
            return Result(); };
        return ports;
    }
    Storage storage() {
        Storage ports; ports.context = this;
        ports.read = [](void* p, const char* name, std::size_t offset, std::uint8_t* data, std::size_t capacity, std::size_t& copied, std::size_t& total) {
            World& w = self(p); copied = total = 0;
            if (w.read_error == name) return StorageResult::Error;
            const auto it = w.files.find(name); if (it == w.files.end()) return StorageResult::Missing;
            total = it->second.size(); if (offset > total) return StorageResult::Error;
            copied = std::min(capacity, total - offset); if (copied) std::memcpy(data, it->second.data() + offset, copied); return StorageResult::Ok; };
        ports.write_atomic = [](void* p, const char* name, const std::uint8_t* data, std::size_t length) {
            World& w = self(p); ++w.writes;
            if (w.fail_write && !--w.fail_write) return StorageResult::Error;
            if (w.torn_write && !--w.torn_write) length /= 2;
            w.files[name] = std::vector<std::uint8_t>(data, data + length); return StorageResult::Ok; };
        ports.remove = [](void* p, const char* name) { return self(p).files.erase(name) ? StorageResult::Ok : StorageResult::Missing; };
        return ports;
    }
    TelemetryIo telemetry_io() {
        TelemetryIo io; io.context = this;
        io.begin = [](void* p, const char*, std::uint16_t) { World& w = self(p); ++w.tx_begin; w.sent.clear(); w.received_at = 0; return true; };
        io.poll_connect = [](void* p) { return self(p).tx_connect; };
        io.send = [](void* p, const std::uint8_t* data, std::size_t length) { World& w = self(p); ++w.tx_calls;
            w.tx_max_send = std::max(w.tx_max_send, unsigned(length));
            if (w.tx_send_limit < 0) return w.tx_send_limit;
            const unsigned count = std::min(unsigned(length), unsigned(w.tx_send_limit)); w.sent.append(reinterpret_cast<const char*>(data), count);
            w.tx_bytes_poll += count; return int(count); };
        io.receive = [](void* p, std::uint8_t* data, std::size_t length) { World& w = self(p); ++w.tx_calls;
            if (w.tx_receive_limit < 0) return w.tx_receive_limit;
            const unsigned count = std::min(unsigned(length), std::min(unsigned(w.tx_receive_limit), unsigned(w.received.size() - w.received_at)));
            if (count) std::memcpy(data, w.received.data() + w.received_at, count);
            w.received_at += count; return int(count); };
        io.close = [](void* p) { ++self(p).tx_close; }; return io;
    }
    void reboot() {
        engine.reset(); snapshot.busy = snapshot.recording = false; command.clear(); state = "idle";
        Clock clock; clock.context = this; clock.millis = [](void* p) { return self(p).time; };
        engine.reset(new Engine(device(), storage(), clock)); engine->set_telemetry_io(telemetry_io()); status = engine->begin().status;
    }
    void poll() { tx_bytes_poll = 0; engine->poll(); tx_max_poll = std::max(tx_max_poll, tx_bytes_poll); }
};
}
extern "C" {
void* world_new(unsigned model) { return new World(model); }
void world_delete(void* p) {
    World& world=World::self(p);
    if(world.http)pico_http_server_initialize(world.http.get(),nullptr,nullptr,nullptr,nullptr,nullptr,0);
    delete &world;
}
void world_pairing_mode(void* p,bool physical) { World::self(p).pairing_mode=physical?1:0; }
unsigned world_status(void* p) { return World::self(p).status; }
const char* world_request(void* p, unsigned method, const char* path, const char* body, unsigned auth) {
    World& w = World::self(p); PicoHttpRequest request = {}; PicoHttpResponse response = {}; char arena[PICO_HTTP_ARENA_CAPACITY] = {};
    request.method = static_cast<PicoHttpMethod>(method); std::snprintf(request.path, sizeof(request.path), "%s", path);
    request.auth_configured = (auth & 1) != 0; request.authenticated = (auth & 2) != 0;
    if (body) { request.body_length = std::strlen(body); if (request.body_length < sizeof(arena)) std::memcpy(arena, body, request.body_length); }
    request.body = arena;
    if (!w.engine->handle(request, response, arena, sizeof(arena))) { w.status = 404; w.output = "{}"; }
    else { w.status = response.status; w.output.assign(reinterpret_cast<const char*>(response.body), response.body_length); }
    return w.output.c_str();
}
void world_tick(void* p, unsigned milliseconds, unsigned times) { World& w = World::self(p); while (times--) { w.time += milliseconds; w.poll(); } }
void world_reboot(void* p) { World::self(p).reboot(); }
void world_fault(void* p, unsigned kind, unsigned value) {
    World& w = World::self(p);
    if (kind == 0) w.fail_write = value;
    if (kind == 1) w.torn_write = value;
    if (kind == 2) w.settings_fail = value;
    if (kind == 3) w.prepare = value;
    if (kind == 4) w.display_changes = value;
    if (kind == 5) w.time = value;
    if (kind == 6) { w.snapshot.temperature_f.valid = true; w.snapshot.temperature_f.value = std::nan(""); }
    if (kind == 7) w.snapshot.phase = value;
    if (kind == 8) w.tx_connect = static_cast<int>(value);
    if (kind == 9) w.tx_send_limit = static_cast<int>(value);
    if (kind == 10) w.tx_receive_limit = static_cast<int>(value);
    if (kind == 11) w.history_interval = value;
}
void world_file(void* p, const char* name, unsigned action) {
    World& w = World::self(p);
    if (!action) w.files.erase(name);
    if (action == 1) { auto found = w.files.find(name); if (found != w.files.end() && !found->second.empty()) found->second.back() ^= 1; }
    if (action == 2) w.read_error = name;
    if (action == 3) w.read_error.clear();
}
unsigned world_metric(void* p, unsigned kind) {
    World& w = World::self(p);
    if (!kind) return w.start_count;
    if (kind == 1) return w.stop_count;
    if (kind == 2) return w.writes;
    if (kind == 3) return unsigned(w.files.size());
    if (kind == 4) { unsigned count = 0; for (const auto& file : w.files) count += file.second.size(); return count; }
    if (kind == 5) return w.tx_begin;
    if (kind == 6) return w.tx_close;
    if (kind == 7) return w.tx_max_send;
    if (kind == 8) return w.tx_max_poll;
    if (kind == 9) return w.resumes;
    if (kind == 10) return w.discards;
    return 0;
}
const char* world_sent(void* p) { return World::self(p).sent.c_str(); }
void world_response(void* p, const char* response) { World::self(p).received = response; }
unsigned world_prepare(void* p, unsigned token) { return World::self(p).engine->prepare_session("start", "", token).status; }
unsigned world_care(void* p, unsigned event) { return World::self(p).engine->record_care(static_cast<CareEvent>(event)).status; }
void world_cancel(void* p) { World::self(p).engine->cancel_prepared_session(); }
void world_recovery(void* p, unsigned kind) {
    Recovery& r = World::self(p).snapshot.recovery; r = Recovery();
    if (!kind) return;
    r.supported = true; r.available = kind != 1;
    if (kind == 1) return;
    if (kind == 3) { r.valid = false; r.available = false; return; }
    std::strcpy(r.recipe_name, "Interrupted ale"); std::strcpy(r.step_name, "Mash");
    r.current_step = 2; r.step_seconds = 1234; r.elapsed_seconds = UINT32_MAX;
    if (kind == 4) std::memset(r.recipe_name, 'x', sizeof(r.recipe_name));
    if (kind == 5) r.step_seconds = -1;
    if (kind == 6) r.current_step = -2;
    if (kind == 7) { r.step_name[0] = 0; r.current_step = -1; r.step_seconds = 0; }
    if (kind == 8) r.recipe_name[0] = 0;
}
unsigned world_preferences(void* p, char unit) { Settings s = World::self(p).settings; s.temperature_unit = unit; return World::self(p).engine->preferences_store(s, TemperatureUnit).status; }
bool world_legacy_codec_test() {
    using namespace picobrew::standalone::detail;
    World world(1); Records records(world.storage()); const unsigned fields = 17;
    std::uint8_t legacy[12 + 2 * fields * 4] = {}; put32(legacy, 77); legacy[4] = fields; legacy[5] = 2;
    for (unsigned row = 0; row < 2; ++row) for (unsigned field = 0; field < fields; ++field) {
        std::uint32_t bits = 0; const float value = 4.5f; std::memcpy(&bits, &value, 4);
        if (field == 0) bits = UINT32_MAX;
        if (field == 4) bits = 0x80000000u;
        if (field >= 5 && field <= 10) bits = 255;
        if (field == 12) bits = 0x7fc00000u;
        put32(legacy + 12 + (row * fields + field) * 4, bits);
    }
    std::uint32_t generation = 0;
    if (records.save("p000", legacy, sizeof(legacy), 0, generation) != RecordResult::Ok) return false;
    Point point;
    if (!point_load(records, 0, 77, fields, point) || point.values[0] != UINT32_MAX || point.values[4] != INT32_MIN ||
        point.values[10] != 255 || ((point.valid >> 12) & 1)) return false;
    if (!point_save(records, 2, point)) return false;
    for (unsigned row = 0; row < 3; ++row) {
        Point read;
        if (!point_load(records, row, 77, fields, read) || read.valid != point.valid) return false;
        for (unsigned field = 0; field < fields; ++field)
            if (((read.valid >> field) & 1) && read.values[field] != point.values[field]) return false;
    }
    std::uint8_t packed[4096]; std::size_t length = 0;
    if (records.load("p000", packed, sizeof(packed), length, generation) != RecordResult::Ok || generation != 2 ||
        packed[6] != 1 || length != 12 + 3 * (fields * 4 - 18)) return false;
    point.values[5] = 256;
    if (point_save(records, 3, point)) return false;
    // Reclaiming a previously wider schema must verify its complete old bank
    // without requiring its old maximum-sized payload in the new allocation.
    std::uint8_t widest[12 + ChunkPoints * FieldCapacity * 4] = {};
    put32(widest, 88); widest[4] = FieldCapacity; widest[5] = ChunkPoints;
    generation = 0;
    if (records.save("p001", widest, sizeof(widest), 0, generation) != RecordResult::Ok) return false;
    point.id = 89; point.values[5] = 255;
    if (!point_save(records, ChunkPoints, point) || !point_load(records, ChunkPoints, 89, fields, point)) return false;
    // Exact range reads retain the complete-bank CRC and generation checks.
    std::uint8_t prefix = 0;
    if (records.load_slice("p000", 4, &prefix, 1, length, 2) != RecordResult::Ok || prefix != fields ||
        records.load_slice("p000", length, &prefix, 1, length, 2) != RecordResult::Corrupt ||
        records.load_slice("p000", 0, &prefix, 1, length + 1, 2) != RecordResult::Corrupt ||
        records.load_slice("p000", 0, &prefix, 1, length, 99) != RecordResult::Corrupt) return false;
    return true;
}
const char* world_http(void* p, const char* request, bool provisioned) {
    struct Wire {
        std::string input, output; std::size_t offset = 0; bool accepted = false, closed = false;
    } wire;
    wire.input = request;
    static const PicoHttpIo io = {
        [](void* p) -> int { Wire& w = *static_cast<Wire*>(p); if (w.accepted) return PICO_HTTP_IO_AGAIN; w.accepted = true; return 0; },
        [](void* p, int, std::uint8_t* data, std::size_t capacity) -> int {
            Wire& w = *static_cast<Wire*>(p); const std::size_t count = std::min(std::min(capacity, std::size_t(11)), w.input.size() - w.offset);
            if (!count) return PICO_HTTP_IO_AGAIN;
            std::memcpy(data, w.input.data() + w.offset, count); w.offset += count; return count;
        },
        [](void* p, int, const std::uint8_t* data, std::size_t length) -> int {
            Wire& w = *static_cast<Wire*>(p); const std::size_t count = std::min(length, std::size_t(17));
            w.output.append(reinterpret_cast<const char*>(data), count); return count;
        },
        [](void* p, int) { static_cast<Wire*>(p)->closed = true; }
    };
    World& world = World::self(p);
    auto& server=world.http;
    if(!server){
        server.reset(new PicoHttpServer);
        pico_http_server_initialize(server.get(), &io, &wire, Engine::http_handler, world.engine.get(), nullptr, 0);
        if(world.pairing_mode>=0)pico_http_server_enable_pairing(server.get(),world.pairing_mode!=0);
    }
    server->io_context=&wire;
    server->handler_context=world.engine.get();
    if(provisioned!=bool(server->key[0]))
        pico_http_server_set_key(server.get(),provisioned?"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef":nullptr,64);
    for (unsigned i = 0; i < 6000 && !wire.closed; ++i) { pico_http_server_poll(server.get(), world.time++); world.poll(); }
    pico_http_server_disconnect(server.get()); world.output = wire.output; return world.output.c_str();
}
}

#ifdef ENGINE_SANITIZER_MAIN
#include <cassert>
int main() {
    assert(world_legacy_codec_test());
    for (unsigned model = 0; model < 3; ++model) {
        void* world = world_new(model);
        assert(world_status(world) == 200);
        world_request(world, PICO_HTTP_GET, "/api/v1/status", nullptr, 3);
        assert(world_status(world) == 200);
        if (!model) {
            for (unsigned kind = 0; kind <= 8; ++kind) {
                world_recovery(world, kind);
                world_request(world, PICO_HTTP_GET, "/api/v1/status", nullptr, 3);
                assert(world_status(world) == ((kind == 4 || kind == 5 || kind == 6 || kind == 8) ? 503u : 200u));
            }
            world_recovery(world, 0);
        }
        const char* seed = "{\"command\":\"start\",\"value\":300}";
        std::uint32_t random = 0x184021u;
        for (unsigned n = 0; n < 1500; ++n) {
            std::string body(seed);
            for (unsigned k = 0; k < 4; ++k) {
                random = random * 1664525u + 1013904223u;
                body[random % body.size()] = static_cast<char>(32 + (random >> 8) % 95);
            }
            world_request(world, PICO_HTTP_POST, "/api/v1/commands", body.c_str(), 3);
        }
        if (model) {
            world_request(world, PICO_HTTP_POST, "/api/v1/commands", "{\"command\":\"start\"}", 3);
            assert(world_status(world) == 202);
            world_tick(world, 15000, 150);
            for (unsigned n = 0; n < 150; n += 2) {
                char path[80]; std::snprintf(path, sizeof(path), "/api/v1/history/samples/%u?id=1", n);
                world_request(world, PICO_HTTP_GET, path, nullptr, 3); assert(world_status(world) == 200);
            }
            world_reboot(world); assert(world_status(world) == 200);
        }
        world_delete(world);
    }
    std::puts("Common engine sanitizer replay passed: 4500 malformed requests, all three models, persisted sample pages and restart");
}
#endif
