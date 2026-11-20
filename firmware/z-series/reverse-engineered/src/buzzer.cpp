#include "zseries/buzzer.hpp"

namespace zseries { namespace oem {
namespace {
const BuzzerCommandDefinition commands[] = {
    {BuzzerCommand::Information, true, "i", "Displays Buzzer state"},
    {BuzzerCommand::On, true, "on", "Turn buzzer on"},
    {BuzzerCommand::Off, true, "off", "Turn buzzer off"},
    {BuzzerCommand::Frequency, false, "setf ", "Sets buzzer frequency"},
    {BuzzerCommand::Duty, false, "setd ", "Sets buzzer duty cycle"}
};
}
const BuzzerCommandDefinition* buzzer_commands(std::size_t& count) noexcept {
    count = sizeof commands / sizeof commands[0];
    return commands;
}
BuzzerResult Buzzer::register_commands() {
    if (!services.begin_console || !services.add_command) return BuzzerResult::MissingService;
    if (!services.begin_console(services.context, "Buzzer")) return BuzzerResult::ServiceFailure;
    for (const auto& value : commands)
        if (!services.add_command(services.context, *this, value)) return BuzzerResult::ServiceFailure;
    return BuzzerResult::Ok;
}
BuzzerResult Buzzer::setup() {
    if (!services.attach || !services.configure || !services.write ||
        !services.begin_console || !services.add_command) return BuzzerResult::MissingService;
    if (!services.attach(services.context, state.pin, state.channel) ||
        !services.configure(services.context, state.channel,
                            static_cast<double>(state.frequency_hz), state.resolution_bits) ||
        !services.write(services.context, state.channel, 0)) return BuzzerResult::ServiceFailure;
    return register_commands();
}
BuzzerResult Buzzer::set_enabled(bool enabled) {
    if (state.enabled == enabled) return BuzzerResult::Ok;
    if (enabled) {
        if (!services.configure) return BuzzerResult::MissingService;
        if (!services.configure(services.context, state.channel,
                                static_cast<double>(state.frequency_hz), state.resolution_bits))
            return BuzzerResult::ServiceFailure;
    }
    state.enabled = enabled;
    return BuzzerResult::Ok;
}
BuzzerResult Buzzer::tick() {
    if (!services.millis) return BuzzerResult::MissingService;
    services.millis(services.context);
    if (state.previous_enabled || state.enabled) {
        if (!services.write) return BuzzerResult::MissingService;
        state.previous_enabled = state.enabled;
        state.phase = state.enabled ? !state.phase : false;
        if (!services.write(services.context, state.channel, state.phase ? state.duty : 0))
            return BuzzerResult::ServiceFailure;
    }
    return BuzzerResult::Ok;
}
BuzzerResult Buzzer::command(BuzzerCommand value, const char* argument) {
    switch (value) {
    case BuzzerCommand::Information:
        if (!services.millis || !services.diagnostic) return BuzzerResult::MissingService;
        return services.diagnostic(services.context, BuzzerDiagnostic::Information,
            services.millis(services.context), state, false) ? BuzzerResult::Ok : BuzzerResult::ServiceFailure;
    case BuzzerCommand::On: return set_enabled(true);
    case BuzzerCommand::Off: return set_enabled(false);
    case BuzzerCommand::Frequency:
    case BuzzerCommand::Duty: {
        if (!services.decimal_integer || !argument) return BuzzerResult::MissingService;
        const auto number = static_cast<std::uint32_t>(services.decimal_integer(services.context, argument));
        if (value == BuzzerCommand::Frequency) state.frequency_hz = number;
        else state.duty = number;
        return BuzzerResult::Ok;
    }
    }
    return BuzzerResult::ServiceFailure;
}
BuzzerResult Buzzer::set_alarm(SystemAlarmState& alarm, bool enabled) {
    if (alarm.enabled != state.enabled) {
        assertion_line = 0x118;
        if (!services.assertion) return BuzzerResult::MissingService;
        if (!services.assertion(services.context, assertion_line)) return BuzzerResult::ServiceFailure;
    }
    if (alarm.enabled == enabled) return BuzzerResult::Ok;
    if (!services.millis || !services.diagnostic || (enabled && !state.enabled && !services.configure))
        return BuzzerResult::MissingService;
    if (!services.diagnostic(services.context, BuzzerDiagnostic::AlarmChanged,
                            services.millis(services.context), state, enabled))
        return BuzzerResult::ServiceFailure;
    alarm.enabled = enabled;
    alarm.changed_ms = services.millis(services.context);
    return set_enabled(enabled);
}
}} // namespace zseries::oem
