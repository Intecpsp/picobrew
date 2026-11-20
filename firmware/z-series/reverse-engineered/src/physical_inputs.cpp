#include "zseries/physical_inputs.hpp"

namespace zseries { namespace oem {
namespace {
std::int32_t signed_word(std::uint32_t value) noexcept {
    return value <= 0x7fffffffu ? static_cast<std::int32_t>(value)
        : static_cast<std::int32_t>(static_cast<std::int64_t>(value) - 0x100000000ll);
}
}

PhysicalInputResult PhysicalButton::setup() {
    if (state.pin < 0) return PhysicalInputResult::Ok;
    if (!io.pin_mode || !io.read_pin) return PhysicalInputResult::MissingService;
    const auto pin = static_cast<std::uint8_t>(state.pin);
    if (!io.pin_mode(io.context, pin, 1)) return PhysicalInputResult::ServiceFailure;
    std::int32_t level = 0;
    if (!io.read_pin(io.context, pin, level)) return PhysicalInputResult::ServiceFailure;
    state.pressed = level == 0;
    return PhysicalInputResult::Ok;
}

PhysicalInputResult PhysicalButton::transition(std::uint32_t next) {
    if (state.state == next) { state.assertion_line = 0x25; return PhysicalInputResult::Ok; }
    if (!io.millis) return PhysicalInputResult::MissingService;
    state.state = next;
    state.state_date = io.millis(io.context);
    if (next == 0) state.press_count = 0;
    else if (next == 1) ++state.press_count;
    return PhysicalInputResult::Ok;
}

PhysicalInputResult PhysicalButton::edge(bool pressed) {
    if (state.state == 1) {
        if (pressed) { state.assertion_line = 0x3f; return PhysicalInputResult::Ok; }
        return transition(2);
    }
    if (state.state == 0) {
        if (!pressed) return PhysicalInputResult::Ok;
    } else if (state.state == 2) {
        if (!pressed) { state.assertion_line = 0x45; return PhysicalInputResult::Ok; }
    } else return PhysicalInputResult::Ok;
    return transition(1);
}

void PhysicalButton::emit(bool long_press, std::uint32_t count) noexcept {
    CoreButtonEvent event;
    event.id = state.id; event.type = long_press; event.argument = count;
    events.put(event);
}

PhysicalInputResult PhysicalButton::tick(std::uint32_t now) {
    if (state.pin < 0) return PhysicalInputResult::Ok;
    if (!io.read_pin) return PhysicalInputResult::MissingService;
    std::int32_t level = 0;
    if (!io.read_pin(io.context, static_cast<std::uint8_t>(state.pin), level))
        return PhysicalInputResult::ServiceFailure;
    const bool pressed = level == 0;
    if (state.pressed != pressed) {
        state.pressed = pressed;
        const auto result = edge(pressed);
        if (result != PhysicalInputResult::Ok) return result;
    }
    const std::uint32_t delay = state.state == 1 ? 5000u : 100u;
    if ((state.state != 1 && state.state != 2) || now <= state.state_date + delay)
        return PhysicalInputResult::Ok;
    emit(state.state == 1, state.press_count);
    return transition(0);
}

PhysicalEncoder::PhysicalEncoder() noexcept : button(button_events) {
    button.state.pin = 39; button.state.id = 1;
}

PhysicalInputResult PhysicalEncoder::setup() {
    if (!services.register_commands) return PhysicalInputResult::MissingService;
    if (!services.register_commands(services.context, *this)) return PhysicalInputResult::ServiceFailure;
    button.io = io;
    const auto result = button.setup();
    if (result != PhysicalInputResult::Ok) return result;
    if (!io.pin_mode || !io.attach_interrupt) return PhysicalInputResult::MissingService;
    for (const auto pin : {pin_a, pin_b, pin_button})
        if (!io.pin_mode(io.context, pin, 1)) return PhysicalInputResult::ServiceFailure;
    for (const auto pin : {pin_a, pin_b})
        if (!io.attach_interrupt(io.context, pin < 40 ? pin : 255, InputInterrupt::EncoderChange, 3))
            return PhysicalInputResult::ServiceFailure;
    if (!io.attach_interrupt(io.context, pin_button < 40 ? pin_button : 255, InputInterrupt::ButtonFalling, 2))
        return PhysicalInputResult::ServiceFailure;
    std::uint8_t value = 0;
    const auto sampled = sample(value);
    if (sampled != PhysicalInputResult::Ok) return sampled;
    if (value == 3) phase = 4;
    else if (value == 0) phase = 2;
    else if (value == 1) phase = 1;
    else { if (value != 2) assertion_line = 0x9b; phase = 3; }
    previous_phase = phase;
    return PhysicalInputResult::Ok;
}

PhysicalInputResult PhysicalEncoder::sample(std::uint8_t& value) {
    if (!io.read_pin) return PhysicalInputResult::MissingService;
    std::int32_t a = 0, b = 0;
    if (!io.read_pin(io.context, pin_a, a) || !io.read_pin(io.context, pin_b, b))
        return PhysicalInputResult::ServiceFailure;
    value = static_cast<std::uint8_t>(static_cast<std::uint8_t>(a) * 2u + static_cast<std::uint8_t>(b));
    return PhysicalInputResult::Ok;
}

PhysicalInputResult PhysicalEncoder::interrupt() {
    std::uint8_t value = 0;
    const auto result = sample(value);
    if (result != PhysicalInputResult::Ok) return result;
    // Corrupt indices would index outside the original object. Reject that
    // undefined span; the valid ISR path retains the original overwrite rule.
    if (write_index >= 256) return PhysicalInputResult::InvalidRingIndex;
    ring[write_index] = value;
    const std::uint16_t next = static_cast<std::uint16_t>(write_index + 1u);
    write_index = next < 256 ? next : 0;
    return PhysicalInputResult::Ok;
}

PhysicalInputResult PhysicalEncoder::emit_rotation(std::int32_t delta) {
    if (!io.millis || !services.rotation_event) return PhysicalInputResult::MissingService;
    activity_date = io.millis(io.context);
    return services.rotation_event(services.context, delta)
        ? PhysicalInputResult::Ok : PhysicalInputResult::ServiceFailure;
}

PhysicalInputResult PhysicalEncoder::tick(std::uint32_t now) {
    button.io = io;
    const auto button_result = button.tick(now);
    if (button_result != PhysicalInputResult::Ok) return button_result;
    if (button_events.pending) {
        if (!io.millis || !services.button_event) return PhysicalInputResult::MissingService;
        activity_date = io.millis(io.context);
        CoreButtonEvent event;
        button_events.take(event);
        if (!services.button_event(services.context, event)) return PhysicalInputResult::ServiceFailure;
    }
    static const std::uint8_t states[9] = {3, 1, 0, 2, 3, 1, 0, 2, 3};
    std::uint32_t delta = 0;
    while (read_index != write_index) {
        if (read_index >= 256 || write_index >= 256) return PhysicalInputResult::InvalidRingIndex;
        if (phase == 0 || phase >= 8) return PhysicalInputResult::InvalidPhase;
        const auto value = ring[read_index];
        read_index = static_cast<std::uint16_t>((read_index + 1u) & 255u);
        if (states[phase + 1] == value) ++phase;
        else if (states[phase - 1] == value) --phase;
        if (phase == 8) { phase = previous_phase = 4; ++delta; }
        else if (phase == 0) { phase = previous_phase = 4; --delta; }
        else if (phase == 2 && previous_phase == 4) { previous_phase = 2; --delta; }
        else if (phase == 4 && previous_phase == 2) { previous_phase = 4; ++delta; }
        else if (phase == 4 && previous_phase == 6) { previous_phase = 4; --delta; }
        else if (phase == 6 && previous_phase == 4) { previous_phase = 6; ++delta; }
    }
    if (delta != 0) {
        const auto result = emit_rotation(signed_word(delta));
        if (result != PhysicalInputResult::Ok) return result;
        total += delta;
    }
    return PhysicalInputResult::Ok;
}

void PhysicalEncoder::console_press() noexcept { button.emit(false, 1); }
PhysicalInputResult PhysicalEncoder::console_rotate(std::int32_t delta) { return emit_rotation(delta); }

}} // namespace zseries::oem
