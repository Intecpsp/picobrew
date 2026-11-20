#include "engine_internal.hpp"

namespace picobrew { namespace standalone {
using namespace detail;
namespace {
constexpr unsigned LegacySessionBytes = 88;
constexpr unsigned SessionBytes = 92;
constexpr unsigned HistoryBytes = 4 + SessionCapacity * SessionBytes;
bool contains_chunk(const Session& session, unsigned point) {
    if (!session.id) return false;
    const unsigned chunks = session.count ? (session.count + ChunkPoints - 1) / ChunkPoints : 1;
    const unsigned distance = (point / ChunkPoints + ChunkCapacity - session.first / ChunkPoints) % ChunkCapacity;
    return distance < chunks;
}
void erase(Session& session) {
    const std::uint32_t generation = session.metadata_generation;
    session = Session(); session.metadata_generation = generation;
}
std::uint32_t signature(const Snapshot& snapshot) {
    std::uint32_t value = 2166136261u;
    const std::uint32_t values[] = {snapshot.device_state, snapshot.phase, snapshot.controller_status,
        snapshot.compartment, static_cast<std::uint32_t>(snapshot.current_step),
        snapshot.ready ? 1u : 0u, snapshot.busy ? 1u : 0u, snapshot.stopped ? 1u : 0u};
    for (std::uint32_t field : values) value = (value ^ field) * 16777619u;
    for (const char* p = snapshot.state; p && *p; ++p) value = (value ^ static_cast<unsigned char>(*p)) * 16777619u;
    return value;
}
}
bool Engine::Impl::save_history(const Session* entries, unsigned next) {
    Bytes bytes(HistoryBytes); if (!bytes.data || next >= PointCapacity) return false;
    put32(bytes.data, next);
    for (unsigned i = 0; i < SessionCapacity; ++i) {
        const Session& s = entries[i]; std::uint8_t* p = bytes.data + 4 + i * SessionBytes;
        put32(p, s.id); put32(p + 4, s.duration); put32(p + 8, s.gaps); put32(p + 12, s.metadata_generation);
        p[16] = s.first; p[17] = s.first >> 8; p[18] = s.count; p[19] = s.count >> 8;
        p[20] = s.fields; p[21] = static_cast<unsigned>(s.state);
        p[22] = (s.truncated ? 1 : 0) | (s.storage_error ? 2 : 0) | (s.recovered ? 4 : 0);
        std::memcpy(p + 24, s.command, 32); std::memcpy(p + 56, s.recipe, 32);
        put32(p + 88, s.interval_ms);
    }
    std::uint32_t generation = 0;
    if (records.save("history", bytes.data, bytes.size, history_generation, generation) != RecordResult::Ok) return false;
    history_generation = generation; next_point = next;
    if (entries != sessions) std::memcpy(sessions, entries, sizeof(sessions));
    return true;
}
bool Engine::Impl::load_history() {
    Bytes bytes(HistoryBytes); if (!bytes.data) return false;
    std::size_t length = 0;
    const RecordResult result = records.load("history", bytes.data, bytes.size, length, history_generation);
    if (result == RecordResult::Missing) return true;
    if (result != RecordResult::Ok || (length != bytes.size && length != 4 + SessionCapacity * LegacySessionBytes) || get32(bytes.data) >= PointCapacity) return false;
    const unsigned stride = length == bytes.size ? SessionBytes : LegacySessionBytes;
    next_point = get32(bytes.data); bool chunks[ChunkCapacity] = {};
    Layout* layout = new (std::nothrow) Layout;
    if (!layout) return false;
    bool good = true;
    for (unsigned i = 0; i < SessionCapacity && good; ++i) {
        Session& s = sessions[i]; const std::uint8_t* p = bytes.data + 4 + i * stride;
        s.id = get32(p); s.duration = get32(p + 4); s.gaps = get32(p + 8); s.metadata_generation = get32(p + 12);
        s.first = p[16] | (unsigned(p[17]) << 8); s.count = p[18] | (unsigned(p[19]) << 8);
        s.fields = p[20]; s.state = static_cast<SessionState>(p[21]);
        s.truncated = (p[22] & 1) != 0; s.storage_error = (p[22] & 2) != 0; s.recovered = (p[22] & 4) != 0;
        std::memcpy(s.command, p + 24, 32); std::memcpy(s.recipe, p + 56, 32);
        s.interval_ms = stride == SessionBytes ? get32(p + 88) : SamplePeriod;
        if (!s.id) continue;
        if (s.first >= PointCapacity || s.first % ChunkPoints || s.count > PointCapacity ||
            s.fields < 14 || s.fields > FieldCapacity || !s.metadata_generation || p[21] < 1 || p[21] > 3 ||
            p[22] > 7 || s.interval_ms < SamplePeriod || s.interval_ms > 86400000u ||
            !std::memchr(s.command, 0, 32) || !std::memchr(s.recipe, 0, 32)) { good = false; break; }
        for (unsigned j = 0; j < i; ++j) if (sessions[j].id == s.id) good = false;
        const unsigned used = s.count ? (s.count + ChunkPoints - 1) / ChunkPoints : 1;
        for (unsigned n = 0; n < used; ++n) {
            const unsigned chunk = (s.first / ChunkPoints + n) % ChunkCapacity;
            if (chunks[chunk]) good = false;
            chunks[chunk] = true;
        }
        char name[12]; ::snprintf(name, sizeof(name), "m%02u", i);
        if (!layout_load(records, name, *layout, s.metadata_generation, s.fields)) good = false;
        if (s.count) { Point point; if (!point_load(records, (s.first + s.count - 1) % PointCapacity, s.id, s.fields, point)) good = false; }
    }
    delete layout; return good;
}
int Engine::Impl::newest_session() const {
    int newest = -1;
    for (unsigned i = 0; i < SessionCapacity; ++i)
        if (sessions[i].id && (newest < 0 || sessions[i].id > sessions[newest].id)) newest = i;
    return newest;
}
void Engine::Impl::history_failure(const char* message) {
    persistent = false;
    if (active >= 0) { sessions[active].storage_error = true; if (sessions[active].gaps != UINT32_MAX) ++sessions[active].gaps; }
    owner.event("history", message);
}
bool Engine::Impl::history_start(bool reserve_only) {
    if (!persistent || sequence == UINT32_MAX || history_interval() < SamplePeriod || history_interval() > 86400000u) return false;
    Layout* layout = new (std::nothrow) Layout;
    if (!layout || !layout_create(snapshot, *layout)) { delete layout; history_failure("invalid sensor layout"); return false; }
    Session staged[SessionCapacity]; std::memcpy(staged, sessions, sizeof(staged));
    int slot = -1;
    for (unsigned i = 0; i < SessionCapacity; ++i) if (!staged[i].id) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (unsigned i = 1; i < SessionCapacity; ++i) if (staged[i].id < staged[slot].id) slot = i;
        erase(staged[slot]);
    }
    const unsigned first = ((next_point + ChunkPoints - 1) / ChunkPoints * ChunkPoints) % PointCapacity;
    bool evicted = sessions[slot].id != 0;
    for (unsigned i = 0; i < SessionCapacity; ++i) if (contains_chunk(staged[i], first)) { erase(staged[i]); evicted = true; }
    // Remove references before any chunk or schema bank can be overwritten.
    if (evicted && !save_history(staged, next_point)) { delete layout; history_failure("catalogue eviction failed"); return false; }
    std::uint8_t bytes[4]; put32(bytes, sequence + 1); std::uint32_t generation = 0;
    if (records.save("seq", bytes, sizeof(bytes), sequence_generation, generation) != RecordResult::Ok) {
        delete layout; history_failure("session identity write failed"); return false;
    }
    ++sequence; sequence_generation = generation;
    Session& s = staged[slot]; const std::uint32_t previous = s.metadata_generation; s = Session();
    s.id = sequence; s.first = first; s.fields = layout->count; s.state = SessionState::Recording;
    s.interval_ms = history_interval();
    copy(s.command, sizeof(s.command), snapshot.command); copy(s.recipe, sizeof(s.recipe), snapshot.recipe_name);
    char name[12]; ::snprintf(name, sizeof(name), "m%02u", static_cast<unsigned>(slot));
    if (!layout_save(records, name, *layout, previous, s.metadata_generation) || !save_history(staged, first)) {
        delete layout; history_failure("session schema write failed"); return false;
    }
    active = slot; prepared = reserve_only; delete active_layout; active_layout = layout;
    started_at = sampled_at = now(); session_token = snapshot.session_token; state_signature = signature(snapshot);
    owner.event("session", reserve_only ? "reserved" : "started"); return reserve_only || history_sample();
}
bool Engine::Impl::history_sample() {
    if (active < 0 || !active_layout || !persistent) return false;
    Session& current = sessions[active];
    const std::uint32_t elapsed = active_elapsed();
    // A record-capacity truncation must still latch a later elapsed-time
    // overflow. Once saturated, the duration can never return to an earlier
    // value as the underlying 32-bit clock completes another cycle.
    if (current.truncated && (elapsed != UINT32_MAX || current.duration == UINT32_MAX)) return true;
    if (current.count >= PointCapacity || elapsed == UINT32_MAX) {
        current.truncated = true; current.duration = elapsed;
        if (!save_history(sessions, next_point)) history_failure("truncation checkpoint failed");
        owner.event("history", elapsed == UINT32_MAX ? "elapsed time capacity reached" : "record capacity reached"); return persistent;
    }
    const unsigned slot = (current.first + current.count) % PointCapacity;
    Session staged[SessionCapacity]; std::memcpy(staged, sessions, sizeof(staged)); bool evicted = false;
    if (!(slot % ChunkPoints)) {
        for (unsigned i = 0; i < SessionCapacity; ++i)
            if (static_cast<int>(i) != active && contains_chunk(staged[i], slot)) { erase(staged[i]); evicted = true; }
    }
    if (evicted && !save_history(staged, next_point)) { history_failure("record reclamation failed"); return false; }
    Point point; point_create(snapshot, *active_layout, current.id, elapsed, point);
    const std::uint32_t periods = (now() - sampled_at) / current.interval_ms;
    if (periods > 1) {
        const std::uint32_t missed = periods - 1;
        staged[active].gaps = UINT32_MAX - staged[active].gaps < missed ? UINT32_MAX : staged[active].gaps + missed;
        point.values[9] = static_cast<unsigned>(point.values[9]) | 16u;
    }
    if (!point_save(records, slot, point)) { history_failure("sample write failed"); return false; }
    ++staged[active].count; staged[active].duration = elapsed;
    if (!save_history(staged, (slot + 1) % PointCapacity)) { history_failure("sample checkpoint failed"); return false; }
    sampled_at = now(); state_signature = signature(snapshot); return true;
}
void Engine::Impl::history_finish(SessionState state) {
    if (active < 0) return;
    sessions[active].state = state; sessions[active].duration = active_elapsed();
    if (sessions[active].duration == UINT32_MAX) sessions[active].truncated = true;
    if (persistent && !save_history(sessions, next_point)) history_failure("session completion checkpoint failed");
    active = -1; prepared = false; delete active_layout; active_layout = nullptr; owner.event("session", "completed");
}
void Engine::Impl::history_poll() {
    if (prepared) {
        if (!snapshot.recording) return;
        if (!snapshot.session_token || snapshot.session_token != session_token) {
            Command stop; copy(stop.name, sizeof(stop.name), "stop"); owner.device_.execute(owner.device_.context, stop);
            history_finish(SessionState::Interrupted); recording_blocked = true;
            owner.event("session", "prepared token mismatch"); return;
        }
        prepared = false; started_at = sampled_at = now(); state_signature = signature(snapshot);
        history_sample(); owner.event("session", "started"); return;
    }
    if (active >= 0 && (!snapshot.recording || (snapshot.session_token && snapshot.session_token != session_token))) {
        if (!snapshot.recording && persistent) history_sample();
        history_finish(SessionState::Completed);
    }
    if (!snapshot.recording) { recording_blocked = false; return; }
    if (active < 0) {
        if (!recording_blocked && !history_start()) {
            Command stop; copy(stop.name, sizeof(stop.name), "stop");
            owner.device_.execute(owner.device_.context, stop); recording_blocked = true;
            owner.event("session", "start rejected: history unavailable");
        }
    } else if (persistent && (now() - sampled_at >= sessions[active].interval_ms || signature(snapshot) != state_signature)) history_sample();
}
Result Engine::prepare_session(const char* command, const char* recipe_name, std::uint32_t token) {
    if (!impl_ || !impl_->persistent) return Result(503, "history_storage", "Persistent session storage is unavailable");
    if (!token || !valid_id(command, 32) || (recipe_name && recipe_name[0] && !valid_text(recipe_name, 32)))
        return Result(400, "invalid_session", "Invalid session identity fields");
    if (impl_->active >= 0)
        return impl_->prepared && impl_->session_token == token &&
            !std::strcmp(impl_->sessions[impl_->active].command, command) &&
            !std::strcmp(impl_->sessions[impl_->active].recipe, recipe_name ? recipe_name : "") ?
            Result() : Result(409, "busy", "A session is already recording or reserved");
    Result refreshed = impl_->refresh(); if (!refreshed.ok()) return refreshed;
    if (impl_->snapshot.recording) return Result(409, "busy", "Reserve the session before native recording starts");
    const char* previous_command = impl_->snapshot.command;
    impl_->snapshot.command = command; copy(impl_->snapshot.recipe_name, sizeof(impl_->snapshot.recipe_name), recipe_name);
    impl_->snapshot.session_token = token;
    const bool saved = impl_->history_start(true); impl_->snapshot.command = previous_command;
    return saved ? Result() : Result(503, "history_storage", "The persistent session identity could not be reserved");
}
void Engine::cancel_prepared_session() {
    if (impl_ && impl_->prepared) impl_->history_finish(SessionState::Interrupted);
}
}}
