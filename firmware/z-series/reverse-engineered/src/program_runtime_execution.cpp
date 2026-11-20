#include "zseries/program_runtime.hpp"
#include <cmath>
#include <limits>

namespace zseries { namespace oem {
namespace {
// The estimator's unsigned conversion receives nonnegative differences, or a
// NaN/overflow from sensor arithmetic. Match the target helper's saturation at
// these boundaries while avoiding an out-of-range host C++ conversion.
std::uint32_t estimate_integer(double value) noexcept {
    if (std::isnan(value) || value >= 4294967296.0) return 0xffffffffu;
    if (value <= 0) return 0;
    return static_cast<std::uint32_t>(value);
}

bool entered_blocking(OpCode instruction) noexcept {
    const auto op = static_cast<std::uint32_t>(instruction);
    return op == 0 || op == 13 || op - 14u < 6 || op - 49u < 7 || op - 21u < 3;
}
bool left_blocking(OpCode instruction) noexcept {
    const auto op = static_cast<std::uint32_t>(instruction);
    return op - 54u < 2 || op - 13u < 7 || op - 21u < 3;
}
}

#define Z_RUNTIME_TRY(expression) do { const RuntimeResult result = (expression); \
    if (result != RuntimeResult::Ok) return result; } while (false)

// 0x400d742c and 0x400daf04. This is a T1-drop watch, not an elapsed-time timer.
void ProgramRuntime::update_t1_drop_watch() noexcept {
    TemperatureDropWatch& watch = state.t1_drop_watch;
    if (!watch.enabled) return;
    if (watch.maximum < observed.scalar_958) watch.maximum = observed.scalar_958;
    else if (watch.maximum - observed.scalar_958 > watch.threshold && !watch.triggered)
        watch.triggered = true;
}
void ProgramRuntime::reset_t1_drop_watch() noexcept { state.t1_drop_watch.enabled = false; }

// 0x400d20ac: precedence is significant when multiple conditions are present.
std::uint32_t ProgramRuntime::pause_reason(RuntimeCommandId command) const noexcept {
    if (observed.system_error) return 14;
    if (observed.msp_error) return 15;
    if (observed.too_hot) return 4;
    if (observed.too_cold) return 5;
    if (!state.ignore_monitor_safeties) {
        if (observed.max_hex_wort_delta) return 6;
        if (observed.max_wort_drain_delta) return 7;
        if (observed.max_hex_rise) return 8;
    }
    if (filter_required && observed.filter_absent) return 3;
    if (observed.pico_still_error) return 12;
    return command == RuntimeCommandId::Pause ? 2 : 0;
}

// 0x400e05c8. The repository provides the program and its heating-rate estimate.
// The OEM considers a found program selected even if its subsequent stack load
// is rejected. Keep that behavior and expose the rejected load separately.
RuntimeResult ProgramRuntime::select_program(std::uint32_t id, bool& selected) {
    selected = false;
    if (!services.find_program) return RuntimeResult::MissingService;
    double heating_rate = 0;
    const ProgramView* found = services.find_program(services.context, id, heating_rate);
    if (!found) return RuntimeResult::Ok;
    program = *found;
    if (!services.millis) return RuntimeResult::MissingService;
    state.start_date = services.millis(services.context);
    state.ignore_monitor_safeties = id < 28 && ((0x08000016u >> id) & 1) != 0;
    state.flag_b5 = id != 26 && id != 7 && id != 2 && id != 4;
    state.flag_b6 = id == 28 || id == 5 || id == 6;
    state.flag_b7 = id == 28 || id == 5 || id == 26 || id == 6 || id == 7;
    state.estimated_heating_rate = heating_rate;
    program_load_result = stack.load(program);
    selected = true;
    return RuntimeResult::Ok;
}

// 0x400ec360 / 0x400ec5fc. Original quirks retained: WaitDelay uses the same
// estimated duration as WaitTime, unknown estimates accumulate with uint32
// wrap, and WaitTempE reserves a fixed 120 seconds in the pending stack.
std::uint32_t estimate_instructions_seconds(const Instruction* code, std::size_t count,
        bool reverse, std::uint32_t seconds, std::uint32_t time, double wort, double heating_rate) noexcept {
    double target = wort;
    for (std::size_t i = 0; i < count; ++i) {
        const Instruction instruction = code[reverse ? count - 1 - i : i];
        std::uint32_t duration = instruction_duration_ms(instruction) / 1000;
        switch (instruction.opcode) {
            case OpCode::PrepareBrew:
            case OpCode::Setup: duration = instruction.argument & 0x400 ? 0 : 60; break;
            case OpCode::MoveValve: duration = 14; break;
            case OpCode::SetPumps: duration = 2; break;
            case OpCode::WaitDelay:
            case OpCode::WaitTime: duration = time; break;
            case OpCode::SetTime: time = instruction.argument; break;
            case OpCode::SetTarget:
                (void)instruction_temperature(instruction, target); break;
            case OpCode::ReachTarget:
                (void)instruction_temperature(instruction, target);
                // Continue into the common heating-time estimate below.
                if (wort > target) duration = 0;
                else if (heating_rate <= 0.01) duration = 0xffffffffu;
                else duration = estimate_integer(((target - wort) * 60) / heating_rate);
                wort = target;
                break;
            case OpCode::WaitTempA:
            case OpCode::WaitTempB:
            case OpCode::WaitTempC:
                if (wort > target) duration = 0;
                else if (heating_rate <= 0.01) duration = 0xffffffffu;
                else duration = estimate_integer(((target - wort) * 60) / heating_rate);
                wort = target;
                break;
            case OpCode::WaitTempE: duration = 120; break;
            case OpCode::DrainFor: wort += (static_cast<double>(duration) * -0.585) / 60; break;
            case OpCode::RinseCycle: duration = 70; break;
            default: break;
        }
        seconds += duration;
    }
    return seconds;
}
void ProgramRuntime::update_total_time() noexcept {
    state.total_left_seconds = estimate_instructions_seconds(stack.size() ? &stack.at(0) : nullptr,
        stack.size(), true, state.instruction_left_seconds, state.time_seconds,
        observed.wort_temperature, state.estimated_heating_rate);
}

// The leading RuntimeState update in 0x4010b200. Non-waiting instructions with
// zero duration intentionally retain their previous instruction-left value.
RuntimeResult ProgramRuntime::update_remaining_time(std::uint32_t now) {
    const std::uint32_t duration = instruction_duration_ms(state.instruction);
    if (state.instruction_date && duration) {
        if (now < state.instruction_date) { assertion_line = 0x564; return RuntimeResult::ClockWentBackwards; }
        const std::uint32_t elapsed = now - state.instruction_date;
        state.instruction_left_seconds = elapsed < duration ? (duration - elapsed) / 1000 : 0;
    } else {
        const auto op = static_cast<std::uint32_t>(state.instruction.opcode);
        if (op - 15u < 3) {
            if (observed.heater_target < observed.wort_temperature) state.instruction_left_seconds = 0;
            else if (observed.wort_rate <= 0.01) state.instruction_left_seconds = 0xffffffffu;
            else state.instruction_left_seconds = estimate_integer(
                ((observed.heater_target - observed.wort_temperature) * 60) / observed.wort_rate);
        } else if (state.instruction.opcode == OpCode::WaitTempE) {
            const double delta = observed.wort_temperature - observed.hex_temperature;
            // Sensor temperatures are finite in the recovered acquisition path.
            if (!std::isfinite(delta) || delta <= -2147483648.0 || delta >= 2147483648.0)
                return RuntimeResult::InvalidNumericDomain;
            const std::int32_t truncated = static_cast<std::int32_t>(delta);
            const std::uint32_t magnitude = static_cast<std::uint32_t>(truncated < 0 ? -truncated : truncated);
            state.instruction_left_seconds = (magnitude * 120u) / 10;
        }
    }
    if (state.start_date && state.start_date < now) state.elapsed_seconds = (now - state.start_date) / 1000;
    update_total_time();
    return RuntimeResult::Ok;
}

RuntimeResult ProgramRuntime::defer(const RuntimeCommand& command) {
    if (!services.requeue_command) return RuntimeResult::MissingService;
    services.requeue_command(services.context, command);
    return RuntimeResult::Ok;
}

// Manual-step expansion in 0x4010b200, using 0x400dee30 Quantity conversions.
// The OEM truncates to integer Fahrenheit before converting back to Celsius.
RuntimeResult ProgramRuntime::push_manual_step(const ManualStepParameters& manual) {
    double hold = 0, drain = 0, fahrenheit = 0;
    if (!quantity_in(manual.hold, Unit::Second, hold) ||
        !quantity_in(manual.drain, Unit::Second, drain) ||
        !quantity_in(manual.temperature, Unit::Fahrenheit, fahrenheit)) {
        assertion_line = 0x7d; return RuntimeResult::IncompatibleQuantity;
    }
    if (!std::isfinite(hold) || !std::isfinite(drain) || !std::isfinite(fahrenheit) ||
        hold < -2147483648.0 || hold >= 2147483648.0 || drain < -2147483648.0 || drain >= 2147483648.0 ||
        fahrenheit < -2147483616.0 || fahrenheit >= 2147483648.0) return RuntimeResult::InvalidNumericDomain;
    Z_RUNTIME_TRY(push(OpCode::GetStepParams));
    Z_RUNTIME_TRY(push(OpCode::ShowScreen, 101));
    Z_RUNTIME_TRY(push(OpCode::StepName, 0x8024));
    Z_RUNTIME_TRY(push(OpCode::RequireFilter));
    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
    Z_RUNTIME_TRY(push(OpCode::DrainFor, static_cast<std::uint32_t>(static_cast<std::int32_t>(drain))));
    Z_RUNTIME_TRY(push(OpCode::HoldFor, static_cast<std::uint32_t>(static_cast<std::int32_t>(hold))));
    const double fixed = (static_cast<double>(static_cast<std::int32_t>(fahrenheit) - 32) / 1.8) * 256;
    if (fixed < 0 || fixed >= 4294967296.0) return RuntimeResult::InvalidNumericDomain;
    Z_RUNTIME_TRY(push(OpCode::ReachTarget, static_cast<std::uint32_t>(fixed)));
    if (static_cast<std::int32_t>(manual.location) > 6) {
        assertion_line = 0x16; return RuntimeResult::InvalidValveLocation;
    }
    if (static_cast<std::int32_t>(manual.location) < 0) {
        assertion_line = 0x18; return RuntimeResult::InvalidValveLocation;
    }
    Z_RUNTIME_TRY(push(OpCode::MoveValve, manual.location));
    Z_RUNTIME_TRY(push(OpCode::RequireFilter, 1));
    return push(OpCode::StepName, 0x8025);
}

// 0x4010b200: one event and at most one newly dispatched instruction per tick.
// Event-queue storage and repository ownership remain explicit application
// services. All command decisions and expansion sequences are reconstructed here.
RuntimeResult ProgramRuntime::tick(std::uint32_t now, const RuntimeCommand& command) {
    assertion_line = 0;
    update_t1_drop_watch();
    Z_RUNTIME_TRY(update_remaining_time(now));
    switch (state.id) {
        case RuntimeStateId::Off: return transition(RuntimeStateId::Booting);
        case RuntimeStateId::Booting:
            if (!msp || !msp->state) return RuntimeResult::MissingService;
            return msp->state->moving_valve ? RuntimeResult::Ok : transition(RuntimeStateId::Idle);
        case RuntimeStateId::Idle:
            if (command.id == RuntimeCommandId::Start) {
                if (observed.system_error || observed.msp_error) return RuntimeResult::Ok;
                bool selected = false;
                Z_RUNTIME_TRY(select_program(command.program_id, selected));
                return selected ? transition(RuntimeStateId::Running) : RuntimeResult::Ok;
            }
            if (now < state.state_date) { assertion_line = 0x2d2; return RuntimeResult::ClockWentBackwards; }
            if (now - state.state_date >= 2000 && observed.hex_temperature <= 50 &&
                observed.wort_temperature <= 50 && observed.drain_temperature <= 50)
                return transition(RuntimeStateId::Standby);
            return RuntimeResult::Ok;
        case RuntimeStateId::Standby:
            if (command.id == RuntimeCommandId::Start) {
                Z_RUNTIME_TRY(transition(RuntimeStateId::Idle));
                return defer(command);
            }
            return RuntimeResult::Ok;
        default:
            if (static_cast<std::uint32_t>(state.id) == 5) return RuntimeResult::Ok;
            if (state.id != RuntimeStateId::Running) { assertion_line = 0x561; return RuntimeResult::InvalidRuntimeState; }
    }
    Z_RUNTIME_TRY(update_fan(50));
    bool complete = false;
    Z_RUNTIME_TRY(pending_complete(command.id, now, complete));
    const std::uint32_t reason = pause_reason(command.id);
    bool advance = complete;
    if (!stopping) {
        if (!complete && state.instruction.opcode == OpCode::MspValve &&
            (reason || command.id != RuntimeCommandId::None)) {
            if (command.id != RuntimeCommandId::None) Z_RUNTIME_TRY(defer(command));
            advance = false;
        } else if (!state.paused && reason) {
            if (reason == 8) {
                Z_RUNTIME_TRY(enable_heater(false));
            }
            Z_RUNTIME_TRY(push_resume(now, complete));
            if (reason == 6 || reason == 8) {
                Z_RUNTIME_TRY(push(OpCode::CircFor, 30));
                Z_RUNTIME_TRY(push(OpCode::Heater, 1));
            }
            state.screen = 0;
            Z_RUNTIME_TRY(push(OpCode::Pause, reason));
            advance = true;
        } else {
            switch (command.id) {
                case RuntimeCommandId::Stop:
                    stack.clear(); state.instruction = Instruction{OpCode::Noop, 0}; advance = true; break;
                case RuntimeCommandId::Drain:
                    if (!state.paused) Z_RUNTIME_TRY(push_resume(now, complete));
                    else {
                        if (!state.pause_reason) { assertion_line = 0x591; return RuntimeResult::InvalidRuntimeState; }
                        if (state.pause_reason > 17) { assertion_line = 0x592; return RuntimeResult::InvalidRuntimeState; }
                        Z_RUNTIME_TRY(push(OpCode::WaitUser, state.pause_reason));
                        if (saved_substep_reference) Z_RUNTIME_TRY(push(OpCode::SubStepName, saved_substep_reference));
                        if (saved_step_reference) Z_RUNTIME_TRY(push(OpCode::StepName, saved_step_reference));
                    }
                    Z_RUNTIME_TRY(push(OpCode::FlagClear, 0x1000));
                    Z_RUNTIME_TRY(push(OpCode::StepName, 0x8000));
                    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
                    Z_RUNTIME_TRY(push(OpCode::ShowScreen, 41));
                    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x60));
                    Z_RUNTIME_TRY(push(OpCode::MspValve, 3));
                    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
                    Z_RUNTIME_TRY(push(OpCode::StopPid));
                    Z_RUNTIME_TRY(push(OpCode::StepName, 0x800e));
                    Z_RUNTIME_TRY(push(OpCode::FlagSet, 0x1000)); advance = true; break;
                case RuntimeCommandId::SetDuration:
                    if (state.instruction.opcode == OpCode::GetDelay) state.delay_seconds = command.duration;
                    else if (state.instruction.opcode == OpCode::GetTime) state.time_seconds = command.duration;
                    else if (state.instruction.opcode == OpCode::WaitFor) state.instruction.argument = command.duration;
                    break;
                case RuntimeCommandId::SetTemperature:
                    if (static_cast<std::uint32_t>(state.instruction.opcode) - 15u < 3)
                        state.instruction.opcode = observed.wort_temperature < command.temperature ? OpCode::WaitTempA : OpCode::WaitTempB;
                    Z_RUNTIME_TRY(set_target(command.temperature)); break;
                case RuntimeCommandId::SetLocation:
                    state.manual.location = command.location;
                    Z_RUNTIME_TRY(save_pending(now));
                    if (static_cast<std::int32_t>(command.location) > 6) { assertion_line = 0x16; return RuntimeResult::InvalidValveLocation; }
                    if (static_cast<std::int32_t>(command.location) < 0) { assertion_line = 0x18; return RuntimeResult::InvalidValveLocation; }
                    Z_RUNTIME_TRY(push(OpCode::MoveValve, command.location)); advance = true; break;
                case RuntimeCommandId::ShowSetupInstructions:
                    Z_RUNTIME_TRY(push_setup_instructions(state.flags)); advance = true; break;
                case RuntimeCommandId::RunPreBrewTests:
                    Z_RUNTIME_TRY(push(OpCode::SetScreen));
                    Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x8000));
                    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
                    Z_RUNTIME_TRY(push(OpCode::ShowScreen, 12));
                    Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x70));
                    Z_RUNTIME_TRY(push(OpCode::StopPid));
                    Z_RUNTIME_TRY(push(OpCode::MoveValve, 1));
                    Z_RUNTIME_TRY(push(OpCode::StepName, 0x8016));
                    Z_RUNTIME_TRY(push(OpCode::RequireFilter, 1));
                    Z_RUNTIME_TRY(push(OpCode::SetScreen)); advance = true; break;
                case RuntimeCommandId::StartManualStep:
                    ++state.manual_step_count;
                    state.manual = command.manual;
                    state.manual_step_notification = true;
                    Z_RUNTIME_TRY(push_manual_step(command.manual)); break;
                default: break;
            }
        }
    }
    bool still_interrupted = false;
    Z_RUNTIME_TRY(progress_still(now, advance ? state.instruction.opcode : OpCode::Noop, still_interrupted));
    if (!advance && !still_interrupted) {
        if (state.t1_drop_watch.enabled && state.t1_drop_watch.triggered) {
            reset_t1_drop_watch();
            if (!services.event) return RuntimeResult::MissingService;
            services.event(services.context, RuntimeEventType::Notification, 1, 0);
        }
        return RuntimeResult::Ok;
    }
    if (state.instruction.opcode == OpCode::WaitUser) {
        state.paused = false;
        state.pause_reason = 0;
    }
    bool notify = left_blocking(state.instruction.opcode);
    state.instruction = Instruction{OpCode::Noop, 0};
    if (!stack.size()) {
        if (!stopping) {
            stopping = true;
            state.screen = 0;
            Z_RUNTIME_TRY(push(OpCode::MspValve, 3));
            Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
            Z_RUNTIME_TRY(push(OpCode::StopPid));
            Z_RUNTIME_TRY(push(OpCode::StepName, 0x8007));
            notify = true;
        } else {
            stopping = false;
            Z_RUNTIME_TRY(transition(RuntimeStateId::Idle));
        }
    }
    if (stack.size()) {
        Instruction next{OpCode::Noop, 0};
        Z_RUNTIME_TRY(stack.pop(next));
        Z_RUNTIME_TRY(dispatch(next));
        notify = notify || entered_blocking(next.opcode);
        update_total_time();
    }
    if (notify) {
        if (!services.event) return RuntimeResult::MissingService;
        services.event(services.context, RuntimeEventType::Update, 0, 0);
    }
    return RuntimeResult::Ok;
}

#undef Z_RUNTIME_TRY
}} // namespace zseries::oem
