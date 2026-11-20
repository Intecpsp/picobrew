#include "zseries/physical_inputs.hpp"
#include <vector>
using namespace zseries::oem;
namespace {
struct Fixture {
    PhysicalEncoder encoder;
    std::uint32_t clock = 0;
    const std::int32_t* reads = nullptr;
    std::size_t read_count = 0, position = 0;
    std::vector<std::vector<std::uint32_t>> events;
};
std::uint32_t clock(void* context) {
    auto& f = *static_cast<Fixture*>(context); const auto value = f.clock++;
    f.events.push_back({3, value}); return value;
}
bool mode(void* context, std::uint8_t pin, std::uint8_t value) {
    static_cast<Fixture*>(context)->events.push_back({0, pin, value}); return true;
}
bool read(void* context, std::uint8_t pin, std::int32_t& value) {
    auto& f = *static_cast<Fixture*>(context);
    value = f.position < f.read_count ? f.reads[f.position++] : 1;
    f.events.push_back({1, pin, static_cast<std::uint32_t>(value)}); return true;
}
bool attach(void* context, std::uint8_t pin, InputInterrupt kind, std::uint8_t value) {
    static_cast<Fixture*>(context)->events.push_back({2, pin, static_cast<std::uint32_t>(kind), value}); return true;
}
bool commands(void* context, PhysicalEncoder&) {
    static_cast<Fixture*>(context)->events.push_back({6}); return true;
}
bool button(void* context, const CoreButtonEvent& event) {
    static_cast<Fixture*>(context)->events.push_back({4, event.id, event.type, event.argument}); return true;
}
bool rotation(void* context, std::int32_t delta) {
    static_cast<Fixture*>(context)->events.push_back({5, static_cast<std::uint32_t>(delta)}); return true;
}
}
extern "C" int physical_inputs_test(const std::uint32_t* words, const std::uint8_t* ring,
    const std::int32_t* reads, std::size_t read_count, const std::uint32_t* steps, std::size_t step_count,
    std::uint32_t* output, std::size_t capacity) {
    Fixture f; auto& e = f.encoder; auto& b = e.button.state; auto& q = e.button_events;
    b.pin = static_cast<std::int32_t>(words[0]); b.id = words[1]; b.pressed = words[2]; b.press_count = words[3];
    b.state = words[4]; b.state_date = words[5]; q.pending = words[6];
    q.value.id = words[7]; q.value.type = words[8]; q.value.argument = words[9];
    e.pin_a = words[10]; e.pin_b = words[11]; e.pin_button = words[12]; e.activity_date = words[13];
    e.phase = words[14]; e.previous_phase = words[15]; e.write_index = words[16]; e.read_index = words[17];
    e.total = words[18]; b.assertion_line = words[19]; e.assertion_line = words[20]; f.clock = words[21];
    f.reads = reads; f.read_count = read_count;
    for (unsigned i = 0; i < 256; ++i) e.ring[i] = ring[i];
    e.io.context = &f; e.io.millis = clock; e.io.pin_mode = mode; e.io.read_pin = read; e.io.attach_interrupt = attach;
    e.services.context = &f; e.services.register_commands = commands; e.services.button_event = button; e.services.rotation_event = rotation;
    e.button.io = e.io;
    std::vector<std::uint32_t> results;
    for (std::size_t i = 0; i < step_count; ++i) {
        const auto* s = steps + i * 3; PhysicalInputResult result = PhysicalInputResult::Ok;
        std::uint8_t sample = 0;
        switch (s[0]) {
        case 0: result = e.button.setup(); break;
        case 1: result = e.button.tick(s[1]); break;
        case 2: result = e.button.edge(s[1] != 0); break;
        case 3: result = e.button.transition(s[1]); break;
        case 4: e.button.emit(s[1] != 0, s[2]); break;
        case 5: result = e.setup(); break;
        case 6: result = e.sample(sample); break;
        case 7: result = e.interrupt(); break;
        case 8: result = e.tick(s[1]); break;
        case 9: result = e.emit_rotation(static_cast<std::int32_t>(s[1])); break;
        case 10: e.console_press(); break;
        case 11: result = e.console_rotate(static_cast<std::int32_t>(s[1])); break;
        case 12: break; // The original button-falling ISR is empty.
        default: return 90;
        }
        if (result != PhysicalInputResult::Ok) return 100 + static_cast<int>(result);
        results.push_back(s[0] == 6 ? sample : 0);
    }
    std::vector<std::uint32_t> out = {0, static_cast<std::uint32_t>(b.pin), b.id, b.pressed, b.press_count, b.state, b.state_date,
        q.pending, q.value.id, q.value.type, q.value.argument, e.pin_a, e.pin_b, e.pin_button, e.activity_date,
        e.phase, e.previous_phase, e.write_index, e.read_index, e.total, b.assertion_line, e.assertion_line,
        f.clock, static_cast<std::uint32_t>(f.position)};
    for (const auto value : e.ring) out.push_back(value);
    out.push_back(results.size()); out.insert(out.end(), results.begin(), results.end());
    out.push_back(f.events.size());
    for (const auto& event : f.events) { out.push_back(event.size()); out.insert(out.end(), event.begin(), event.end()); }
    if (out.size() > capacity) return 91;
    out[0] = out.size() - 1;
    for (std::size_t i = 0; i < out.size(); ++i) output[i] = out[i];
    return 0;
}
