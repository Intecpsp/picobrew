// Typed Z119 runtime execution. Original addresses: analysis/program-runtime.md.
#pragma once
#include "zseries/runtime_instruction.hpp"
#include "zseries/console.hpp"
#include "zseries/quantity.hpp"
#include "zseries/msp_protocol.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {

enum class RuntimeResult {
    Ok, StackFull, StackEmpty, InvalidProgram, InvalidStringReference,
    MissingService, ControllerFailure, MonitorSafetiesIgnored,
    UndefinedStillReference, InvalidValveLocation, ClockWentBackwards,
    InvalidSetupMode, InvalidPendingInstruction, InvalidRuntimeState,
    IncompatibleQuantity, InvalidNumericDomain, ServiceFailure
};

struct ProgramView {
    const char* name = nullptr;
    const Instruction* code = nullptr;
    std::size_t code_size = 0;
    const char* const* strings = nullptr;
    std::size_t string_count = 0;
};

const char* program_validation_error(const ProgramView&) noexcept;

// Original 256-instruction LIFO expansion stack. Pushes deliberately use
// reverse execution order. Program loading reuses the recovered validator.
class InstructionStack {
public:
    static constexpr std::size_t capacity = 256;
    RuntimeResult push(Instruction,const ConsoleServices* diagnostics=nullptr) noexcept;
    RuntimeResult push_checked(Instruction,const ConsoleServices* diagnostics=nullptr) noexcept;
    RuntimeResult pop(Instruction&,const ConsoleServices* diagnostics=nullptr) noexcept;
    RuntimeResult load(const ProgramView&,const ConsoleServices* diagnostics=nullptr) noexcept;
    void clear() noexcept;
    std::size_t size() const noexcept { return size_; }
    const Instruction& at(std::size_t index) const noexcept { return code_[index]; }
private:
    std::array<Instruction, capacity> code_{};
    std::size_t size_ = 0;
};

std::uint32_t instruction_duration_ms(Instruction) noexcept;
const char* common_program_string(std::uint32_t reference) noexcept;
// Shared original iterator estimator, used by both Program statistics and the
// runtime's pending stack. `reverse` selects the OEM iterator direction.
std::uint32_t estimate_instructions_seconds(const Instruction*, std::size_t count,
    bool reverse, std::uint32_t initial_seconds, std::uint32_t time_seconds,
    double wort_celsius, double heating_rate) noexcept;

struct RuntimeStillState {
    std::uint32_t id = 1;
    std::uint32_t paused_id = 0;
    std::uint32_t reference_date = 0;
    double reference_wort = 0;
    double reference_t3 = 0;
    double heat_loss = 0;
    double boil_t3 = 0;
    std::uint32_t boil_date = 0;
    double boil_scalar_978 = 0;
    double boil_target = 0;
    std::uint32_t step_date = 0;
    double step_target = 0;
};

struct TemperatureDropWatch {
    bool enabled = false;
    bool triggered = false;
    double maximum = 0;
    double threshold = 0;
};

RuntimeResult drop_watch_start(TemperatureDropWatch&,double,const ConsoleServices* diagnostics=nullptr) noexcept;
RuntimeResult drop_watch_update(TemperatureDropWatch&,double,const ConsoleServices* diagnostics=nullptr) noexcept;
RuntimeResult drop_watch_stop(TemperatureDropWatch&,const ConsoleServices* diagnostics=nullptr) noexcept;

struct ManualStepParameters {
    bool byte_00 = false;
    std::uint32_t location = 1;
    Quantity temperature{160, Unit::Fahrenheit};
    Quantity hold{3600, Unit::Second};
    Quantity drain{300, Unit::Second};
    std::array<std::uint8_t, 4> bytes_38{};
};

struct RuntimeCommand {
    RuntimeCommandId id = RuntimeCommandId::None;
    std::uint32_t program_id = 0;
    double temperature = 0;
    std::uint32_t duration = 0;
    std::uint32_t location = 0;
    ManualStepParameters manual;
};

struct ProgramRuntimeState {
    RuntimeStateId id = RuntimeStateId::Off;
    std::uint32_t state_date = 0;
    std::uint32_t pause_reason = 0;
    std::uint32_t step_count = 0xffffffffu;
    const char* step_name = "";
    std::array<char, 65> step_buffer{};
    std::uint32_t manual_step_count = 0;
    const char* substep_name = "";
    bool paused = false;
    bool pid_active = false;
    std::uint32_t start_date = 0;
    std::uint32_t elapsed_seconds = 0;
    Instruction instruction{OpCode::Noop, 0};
    std::uint32_t instruction_count = 0;
    std::uint32_t instruction_date = 0;
    std::uint32_t instruction_left_seconds = 0;
    std::uint32_t screen = 0;
    double input_temperature_min = 10;
    double input_temperature_max = 93;
    std::uint32_t delay_seconds = 0;
    std::uint32_t time_seconds = 0;
    std::uint32_t total_left_seconds = 0;
    bool ignore_monitor_safeties = false;
    bool flag_b5 = true;
    bool flag_b6 = false;
    bool flag_b7 = false;
    bool manual_step_notification = false;
    bool cooling_to_target = false;
    double estimated_heating_rate = 0;
    std::uint32_t flags = 0;
    RuntimeStillState still;
    TemperatureDropWatch t1_drop_watch;
    ManualStepParameters manual;
};

// Input snapshots from recovered acquisition/state layers. Where a physical
// interpretation remains uncertain the original CoreState offset is retained.
struct RuntimeObserved {
    double heater_target = 0;
    double hex_temperature = 0;
    double wort_temperature = 0;
    double drain_temperature = 0;
    double wort_rate = 0;
    bool filter_absent = false;
    bool pico_still_error = false;
    bool system_error = false;
    bool msp_error = false;
    bool too_hot = false;
    bool too_cold = false;
    bool max_hex_wort_delta = false;
    bool max_wort_drain_delta = false;
    bool max_hex_rise = false;
    bool signal_950_defined = false;
    bool signal_a00_defined = false;
    bool signal_ab0_defined = false;
    bool still_sample_present = false;
    bool byte_980 = false;
    double scalar_958 = 0;
    double scalar_960 = 0;
    double still_t3 = 0;
    double scalar_978 = 0;
    double scalar_9d8 = 0;
    double scalar_a28 = 0;
    double scalar_a98 = 0;
    double scalar_aa8 = 0;
    double scalar_ab8 = 0;
    double scalar_ad8 = 0;
    double scalar_b48 = 0;
    double scalar_b58 = 0;
};

// Missing services cause an error; no default no-op hardware implementations.
struct RuntimeServices {
    void* context = nullptr;
    void* heater_context = nullptr; // null shares the application context
    std::uint32_t (*millis)(void*) = nullptr;
    bool (*heater_enable)(void*, bool) = nullptr;
    bool (*pid_start)(void*) = nullptr;
    bool (*pid_stop)(void*) = nullptr;
    bool (*heater_target)(void*, double) = nullptr;
    void (*still_command)(void*, std::uint32_t, std::uint32_t) = nullptr;
    bool (*still_ready)(void*) = nullptr;
    void (*still_cancel)(void*) = nullptr;
    void (*event)(void*, RuntimeEventType, std::uint32_t, std::uint32_t) = nullptr;
    const ProgramView* (*find_program)(void*, std::uint32_t, double& heating_rate) = nullptr;
    void (*requeue_command)(void*, const RuntimeCommand&) = nullptr;
    // Bound application services execute OEM assertions and their continuation.
    // Empty services preserve the explicitly guarded legacy standalone API.
    ConsoleServices diagnostics;
};

struct ProgramRuntime {
    ProgramRuntimeState state;
    RuntimeObserved observed;
    InstructionStack stack;
    ProgramView program;
    RuntimeServices services;
    z_msp_client* msp = nullptr;
    std::uint32_t requested_valve = 0;
    std::uint8_t requested_pumps = 0;
    std::uint32_t saved_step_reference = 0;
    std::uint32_t saved_substep_reference = 0;
    bool alternative_valve_map = false;
    bool filter_required = false;
    bool stopping = false;
    std::uint32_t assertion_line = 0;
    int controller_result = 0;
    RuntimeResult program_load_result = RuntimeResult::Ok;

    RuntimeResult dispatch(Instruction);
    RuntimeResult transition(RuntimeStateId);
    RuntimeResult transition_still(std::uint32_t);
    RuntimeResult push_setup_instructions(std::uint32_t flags) noexcept;
    RuntimeResult save_pending(std::uint32_t now);
    RuntimeResult push_resume(std::uint32_t now, bool pending_completed);
    RuntimeResult pending_complete(RuntimeCommandId, std::uint32_t now, bool& result);
    RuntimeResult pause_still(std::uint32_t now, bool pending_completed,
                              OpCode wait, std::uint32_t step_reference, bool stop_pumps);
    RuntimeResult progress_still(std::uint32_t now, OpCode completed, bool& interrupted);
    RuntimeResult update_fan(double threshold);
    RuntimeResult start_t1_drop_watch(double threshold) noexcept;
    RuntimeResult update_t1_drop_watch() noexcept;
    RuntimeResult reset_t1_drop_watch() noexcept;
    std::uint32_t pause_reason(RuntimeCommandId) const noexcept;
    RuntimeResult select_program(std::uint32_t id, bool& selected);
    RuntimeResult update_remaining_time(std::uint32_t now);
    void update_total_time() noexcept;
    RuntimeResult tick(std::uint32_t now, const RuntimeCommand& command);

private:
    const ConsoleServices* diagnostics() const noexcept;
    RuntimeResult record_assertion(const char*,std::uint32_t,const char*,RuntimeResult);
    RuntimeResult push(OpCode, std::uint32_t argument = 0) noexcept;
    void* heater_context() const noexcept;
    RuntimeResult enable_heater(bool);
    RuntimeResult start_pid();
    RuntimeResult stop_pid();
    RuntimeResult set_target(double);
    RuntimeResult set_pumps(std::uint8_t);
    RuntimeResult move_valve(std::uint8_t);
    RuntimeResult stop_outputs();
    RuntimeResult push_manual_step(const ManualStepParameters&);
    RuntimeResult defer(const RuntimeCommand&);
    const char* string(std::uint32_t reference) const noexcept;
};

}} // namespace zseries::oem
