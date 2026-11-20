// Z119 Buzzer.h and System::setAlarm. SDK and console operations are explicit.
#pragma once
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

struct BuzzerState {
    std::uint8_t channel = 1, resolution_bits = 8, pin = 13;
    std::uint32_t frequency_hz = 4800, duty = 125;
    bool enabled = false, previous_enabled = false, phase = false;
};
struct SystemAlarmState {
    bool enabled = false;
    std::uint32_t changed_ms = 0;
};
enum class BuzzerCommand : std::uint8_t { Information, On, Off, Frequency, Duty };
struct BuzzerCommandDefinition {
    BuzzerCommand command;
    bool exact_match;
    const char* name;
    const char* description;
};
const BuzzerCommandDefinition* buzzer_commands(std::size_t& count) noexcept;
enum class BuzzerDiagnostic : std::uint8_t { Information, AlarmChanged };
class Buzzer;
struct BuzzerServices {
    void* context = nullptr;
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*attach)(void*, std::uint8_t pin, std::uint8_t channel) = nullptr;
    // The OEM ignores ledcSetup's returned frequency. Completion here reports
    // whether the service was invoked, separately from that SDK return value.
    bool (*configure)(void*, std::uint8_t channel, double frequency,
                      std::uint8_t resolution_bits) = nullptr;
    bool (*write)(void*, std::uint8_t channel, std::uint32_t duty) = nullptr;
    bool (*begin_console)(void*, const char* name) = nullptr;
    bool (*add_command)(void*, Buzzer&, const BuzzerCommandDefinition&) = nullptr;
    // Original ROM atoi is a boundary. Defined comparisons use strings whose
    // parsed signed value fits int32; negative values retain their uint32 bits.
    std::int32_t (*decimal_integer)(void*, const char*) = nullptr;
    // Original Assert::fail logs, increments a global diagnostic counter,
    // delays 200 ms, then RETURNS. This caller-owned service retains those
    // effects and returns true when handled; it must not stop normal flow.
    bool (*assertion)(void*, std::uint32_t source_line) = nullptr;
    bool (*diagnostic)(void*, BuzzerDiagnostic, std::uint32_t timestamp,
                       const BuzzerState&, bool alarm_enabled) = nullptr;
};
enum class BuzzerResult { Ok, MissingService, ServiceFailure };
class Buzzer {
public:
    BuzzerState state;
    BuzzerServices services;
    std::uint32_t assertion_line = 0;

    // Original 0x400f6e78 and 0x400f6d58, including five console commands.
    BuzzerResult setup();
    BuzzerResult register_commands();
    // Original 0x400d141c. Disabling defers the zero-duty write to tick().
    BuzzerResult set_enabled(bool);
    // Original Core timer callback 0x40080570; normally called every second.
    // Its clock query is preserved even when neither enable flag is set.
    BuzzerResult tick();
    BuzzerResult command(BuzzerCommand, const char* argument = nullptr);
    // Original System::setAlarm 0x400dbc40. Mismatched flags call the
    // returning assertion service, then continue. A changed alarm takes
    // two functional clock samples, the latter for changed_ms.
    BuzzerResult set_alarm(SystemAlarmState&, bool enabled);
};

}} // namespace zseries::oem
