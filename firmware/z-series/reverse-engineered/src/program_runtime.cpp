#include "zseries/program_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace zseries { namespace oem {

// Original pointer table 0x3f42a1c0, in reference order 0x8000..0x8025.
const char* common_program_string(std::uint32_t reference) noexcept {
    static const char* const strings[] = {
        "", "Delayed Start", "Preparing Machine", "Preparing To Brew", "Running", "Testing",
        "Finishing", "Stopping", "Final drain", "Rinsing", "Heating", "Holding",
        "Holding Passthrough", "Cooling", "Draining", "Racking", "Circulating", "Reaching Temp",
        "Flushing lines", "KegPump", "Pre-Wetting", "Pre-Brew Tests", "Pre-Brew Tests Circulation",
        "Waiting User", "Measuring Heat Loss", "Finding Boil Point", "Stabilizing", "Distilling",
        "Equalizing", "Temporizing", "Stabilizing Pot Temp", "Sealing", "Establishing Vacuum",
        "Resuming Session", "Still T2 Too High", "Wait Pump Ok", "Configuring Step", "Step %d"
    };
    return is_common_string_reference(reference) ? strings[reference - 0x8000u] : nullptr;
}

namespace {
RuntimeResult stack_assertion(const ConsoleServices* diagnostics,std::uint32_t line,const char* expression,RuntimeResult guarded){
    if(!diagnostics)return guarded;
    if(!diagnostics->assertion)return RuntimeResult::MissingService;
    return diagnostics->assertion(diagnostics->context,"Runtime.h",line,expression)?RuntimeResult::Ok:RuntimeResult::ServiceFailure;
}
}
RuntimeResult InstructionStack::push(Instruction instruction,const ConsoleServices* diagnostics) noexcept {
    if (size_ >= capacity) return stack_assertion(diagnostics,0x43,"codeSize < Program::MaxCodeSize",RuntimeResult::StackFull);
    code_[size_++] = instruction;
    return RuntimeResult::Ok;
}
RuntimeResult InstructionStack::push_checked(Instruction instruction,const ConsoleServices* diagnostics) noexcept {
    if(!instruction_valid(instruction)){
        const auto result=stack_assertion(diagnostics,0x4e,"instr.isValid()",RuntimeResult::InvalidProgram);
        if(result!=RuntimeResult::Ok)return result;
    }
    return push(instruction,diagnostics);
}
RuntimeResult InstructionStack::pop(Instruction& instruction,const ConsoleServices* diagnostics) noexcept {
    if (size_ == 0) {
        const auto result=stack_assertion(diagnostics,0x37,"!isEmpty()",RuntimeResult::StackEmpty);
        if(result!=RuntimeResult::Ok)return result;
        instruction=Instruction{OpCode::Noop,0};return RuntimeResult::Ok;
    }
    instruction = code_[--size_];
    return RuntimeResult::Ok;
}
void InstructionStack::clear() noexcept {
    code_.fill(Instruction{static_cast<OpCode>(0), 0});
    size_ = 0;
}
RuntimeResult InstructionStack::load(const ProgramView& program,const ConsoleServices* diagnostics) noexcept {
    if (program_validation_error(program)) return RuntimeResult::InvalidProgram;
    if (!diagnostics && program.code_size > capacity - size_) return RuntimeResult::StackFull;
    for (std::size_t i = program.code_size; i != 0; --i) {
        const auto result=push(program.code[i - 1],diagnostics);if(result!=RuntimeResult::Ok)return result;
    }
    return RuntimeResult::Ok;
}

// 0x400e01d8; successful validation returns a null diagnostic string.
const char* program_validation_error(const ProgramView& program) noexcept {
    if (!program.name || !program.name[0]) return "Program has no name";
    if (!program.code || program.code_size == 0) return "Program has no instruction";
    if (program.code_size > InstructionStack::capacity) return "Program has too many instructions";
    if ((program.string_count != 0) != (program.strings != nullptr))
        return "Program has inconsistent string table";
    for (std::size_t i = 0; i < program.code_size; ++i) {
        const Instruction instruction = program.code[i];
        if (!instruction_valid(instruction)) return "Program has invalid instruction";
        if (instruction.opcode == OpCode::StepName || instruction.opcode == OpCode::SubStepName) {
            if (!is_common_string_reference(instruction.argument) &&
                (!program.strings || instruction.argument >= program.string_count))
                return "Program has invalid string reference";
        }
    }
    return nullptr;
}

// 0x400d12b0; multiplication deliberately wraps at 32 bits.
std::uint32_t instruction_duration_ms(Instruction instruction) noexcept {
    const std::uint32_t opcode = static_cast<std::uint32_t>(instruction.opcode);
    if (opcode - 3u < 3u || instruction.opcode == OpCode::WaitFor) return instruction.argument * 1000u;
    if (instruction.opcode == OpCode::MspPumps) return 2000;
    if (instruction.opcode == OpCode::MspValve) return 10000;
    return 0;
}

const ConsoleServices* ProgramRuntime::diagnostics() const noexcept {
    return services.diagnostics.assertion||services.diagnostics.log?&services.diagnostics:nullptr;
}
RuntimeResult ProgramRuntime::record_assertion(const char* file,std::uint32_t line,const char* expression,RuntimeResult guarded){
    assertion_line=line;const auto* sink=diagnostics();if(!sink)return guarded;
    if(!sink->assertion)return RuntimeResult::MissingService;
    return sink->assertion(sink->context,file,line,expression)?RuntimeResult::Ok:RuntimeResult::ServiceFailure;
}
RuntimeResult ProgramRuntime::push(OpCode code, std::uint32_t argument) noexcept {
    if(stack.size()==InstructionStack::capacity)assertion_line=0x43;
    RuntimeResult result = stack.push(Instruction{code, argument},diagnostics());
    if (result == RuntimeResult::StackFull) assertion_line = 0x43;
    return result;
}
RuntimeResult ProgramRuntime::stop_pid() {
    if (!services.pid_stop) return RuntimeResult::MissingService;
    return services.pid_stop(heater_context()) ? RuntimeResult::Ok : RuntimeResult::ServiceFailure;
}
void* ProgramRuntime::heater_context() const noexcept {
    return services.heater_context ? services.heater_context : services.context;
}
RuntimeResult ProgramRuntime::enable_heater(bool enabled) {
    if (!services.heater_enable) return RuntimeResult::MissingService;
    return services.heater_enable(heater_context(), enabled) ? RuntimeResult::Ok : RuntimeResult::ServiceFailure;
}
RuntimeResult ProgramRuntime::start_pid() {
    if (!services.pid_start) return RuntimeResult::MissingService;
    return services.pid_start(heater_context()) ? RuntimeResult::Ok : RuntimeResult::ServiceFailure;
}
RuntimeResult ProgramRuntime::set_target(double target) {
    if (!services.heater_target) return RuntimeResult::MissingService;
    return services.heater_target(heater_context(), target) ? RuntimeResult::Ok : RuntimeResult::ServiceFailure;
}
RuntimeResult ProgramRuntime::set_pumps(std::uint8_t flags) {
    if (!msp) return RuntimeResult::MissingService;
    controller_result = z_msp_send(msp, Z_MSP_PUMPS_SET, flags, 1);
    return controller_result == Z_MSP_OK ? RuntimeResult::Ok : RuntimeResult::ControllerFailure;
}
RuntimeResult ProgramRuntime::move_valve(std::uint8_t position) {
    if (!msp) return RuntimeResult::MissingService;
    controller_result = z_msp_move_valve(msp, position);
    return controller_result == Z_MSP_OK ? RuntimeResult::Ok : RuntimeResult::ControllerFailure;
}
RuntimeResult ProgramRuntime::stop_outputs() {
    RuntimeResult result = stop_pid();
    if (result != RuntimeResult::Ok) return result;
    if (!msp || !msp->state) return RuntimeResult::MissingService;
    return set_pumps(msp->state->pumps & 0x40);
}
const char* ProgramRuntime::string(std::uint32_t reference) const noexcept {
    if (is_common_string_reference(reference)) return common_program_string(reference);
    if (reference < program.string_count && program.strings) return program.strings[reference];
    return nullptr;
}

#define Z_RUNTIME_TRY(expression) do { const RuntimeResult result = (expression); \
    if (result != RuntimeResult::Ok) return result; } while (false)

RuntimeResult ProgramRuntime::push_setup_instructions(std::uint32_t flags) noexcept {
    if (!(flags & 0x10)) return push(OpCode::ShowScreen, 15);
    if ((flags & 3) == 0) {
        for (std::uint32_t screen = 9; screen >= 2; --screen) Z_RUNTIME_TRY(push(OpCode::ShowScreen, screen));
    } else if ((flags & 3) == 1) {
        for (std::uint32_t screen = 23; screen >= 16; --screen) Z_RUNTIME_TRY(push(OpCode::ShowScreen, screen));
    }
    return RuntimeResult::Ok;
}

// 0x400e64d0 / 0x400e6428; diagnostics do not affect the state transition.
RuntimeResult ProgramRuntime::transition_still(std::uint32_t id) {
    if (state.still.id == id) return RuntimeResult::Ok;
    state.still.id = id;
    if (id == 4) {
        if (!state.still.reference_date) {
            Z_RUNTIME_TRY(record_assertion("Runtime.h",0x220,"rtState.isRefDefined()",RuntimeResult::UndefinedStillReference));
        }
        if (!services.millis) return RuntimeResult::MissingService;
        const auto step_date=services.millis(services.context);
        const auto step_target=state.still.heat_loss+2.0;
        if(!state.still.reference_date)Z_RUNTIME_TRY(record_assertion("RuntimeState.h",0x41a,"isRefDefined()",RuntimeResult::UndefinedStillReference));
        state.still.step_date=step_date;state.still.step_target=step_target;
    }
    return RuntimeResult::Ok;
}

// 0x4010a48c: dispatch itself does not reject invalid instructions; loading is
// the validation boundary. Unknown opcodes update bookkeeping and are logged.
RuntimeResult ProgramRuntime::dispatch(Instruction instruction) {
    assertion_line = 0;
    if (!services.millis) return RuntimeResult::MissingService;
    state.instruction = instruction;
    ++state.instruction_count;
    state.instruction_date = services.millis(services.context);
    state.instruction_left_seconds = instruction_duration_ms(instruction) / 1000;
    const std::uint32_t arg = instruction.argument;
    double temperature = 0;
    switch (instruction.opcode) {
        case OpCode::Noop:
        case OpCode::WaitFor:
        case OpCode::WaitTempA:
        case OpCode::WaitTempB:
        case OpCode::WaitTempC:
        case OpCode::WaitTempE:
        case OpCode::GetDelay:
        case OpCode::GetStepParams:
        case OpCode::GetTemp:
        case OpCode::GetTime:
        case OpCode::WaitStillReady:
        case OpCode::WaitSealed:
        case OpCode::WaitVacuum:
        case OpCode::WaitT3Stable:
        case OpCode::WaitT1Rise:
        case OpCode::WaitT3Drop:
        case OpCode::WaitT2Safety:
        case OpCode::WaitPumpOk:
            return RuntimeResult::Ok;
        case OpCode::FlagSet: state.flags |= arg; return RuntimeResult::Ok;
        case OpCode::FlagClear: state.flags &= ~arg; return RuntimeResult::Ok;
        case OpCode::WaitUser: state.pause_reason = arg; return RuntimeResult::Ok;
        case OpCode::SetTime: state.time_seconds = arg; return RuntimeResult::Ok;
        case OpCode::SetITMin:
            (void)instruction_temperature(instruction, state.input_temperature_min); return RuntimeResult::Ok;
        case OpCode::SetITMax:
            (void)instruction_temperature(instruction, state.input_temperature_max); return RuntimeResult::Ok;
        case OpCode::SetScreen: state.screen = arg; return RuntimeResult::Ok;
        case OpCode::MspValve:
            requested_valve = arg;
            return move_valve(static_cast<std::uint8_t>(arg));
        case OpCode::MspPumps:
            requested_pumps = static_cast<std::uint8_t>(arg);
            return set_pumps(requested_pumps);
        case OpCode::ValveMode: alternative_valve_map = arg == 1; return RuntimeResult::Ok;
        case OpCode::SetTarget:
            (void)instruction_temperature(instruction, temperature); return set_target(temperature);
        case OpCode::Heater:
            if (state.ignore_monitor_safeties) Z_RUNTIME_TRY(record_assertion("Runtime.h",0x379,"!_rtState.areMonitorSafetiesIgnored",RuntimeResult::MonitorSafetiesIgnored));
            return enable_heater(arg == 1);
        case OpCode::StartPid:
            if (state.ignore_monitor_safeties) Z_RUNTIME_TRY(record_assertion("Runtime.h",0x37c,"!_rtState.areMonitorSafetiesIgnored",RuntimeResult::MonitorSafetiesIgnored));
            Z_RUNTIME_TRY(start_pid()); state.pid_active = true; return RuntimeResult::Ok;
        case OpCode::StopPid:
            Z_RUNTIME_TRY(stop_pid()); state.pid_active = false; return RuntimeResult::Ok;
        case OpCode::RequireFilter: filter_required = arg != 0; return RuntimeResult::Ok;
        case OpCode::StepName: {
            saved_step_reference = arg;
            const char* name = string(arg);
            if (!name) { Z_RUNTIME_TRY(record_assertion("Program.h",0x78,"valid",RuntimeResult::InvalidStringReference));name="<INVALID>"; }
            state.step_name = name;
            state.step_buffer.fill(0);
            if (arg == 0x8025) {
                ::snprintf(state.step_buffer.data(), state.step_buffer.size(),
                           common_program_string(0x8025), static_cast<std::int32_t>(state.manual_step_count));
                state.step_name = state.step_buffer.data();
            } else {
                std::strncpy(state.step_buffer.data(), state.step_name, state.step_buffer.size() - 1);
            }
            ++state.step_count;
            return RuntimeResult::Ok;
        }
        case OpCode::SubStepName: {
            saved_substep_reference = arg;
            const char* name = string(arg);
            if (!name) { Z_RUNTIME_TRY(record_assertion("Program.h",0x78,"valid",RuntimeResult::InvalidStringReference));name="<INVALID>"; }
            state.substep_name = name;
            return RuntimeResult::Ok;
        }
        case OpCode::Pause:
            state.paused = true;
            Z_RUNTIME_TRY(stop_outputs());
            return push(OpCode::WaitUser, arg == 0 ? 1 : arg);
        case OpCode::SetPumps: return push(OpCode::MspPumps, ((arg & 3) << 4) | 0x40);
        case OpCode::MoveValve: {
            static const std::uint8_t normal[] = {3, 2, 4, 5, 6, 8, 9};
            static const std::uint8_t alternative[] = {3, 2, 4, 6, 7, 8, 9};
            if (arg >= sizeof(normal)) return RuntimeResult::InvalidValveLocation;
            if (!msp || !msp->state) return RuntimeResult::MissingService;
            const bool expected_keg = (requested_pumps & 0x10) != 0;
            const bool actual_keg = (msp->state->pumps & 0x10) != 0;
            if (state.pid_active) Z_RUNTIME_TRY(push(OpCode::StartPid));
            if (expected_keg) Z_RUNTIME_TRY(push(OpCode::MspPumps, requested_pumps | 0x10));
            Z_RUNTIME_TRY(push(OpCode::MspValve, (alternative_valve_map ? alternative : normal)[arg]));
            if (expected_keg || actual_keg) Z_RUNTIME_TRY(push(OpCode::MspPumps, requested_pumps & 0xef));
            if (state.pid_active) Z_RUNTIME_TRY(push(OpCode::StopPid));
            return RuntimeResult::Ok;
        }
        case OpCode::NoTarget: return push(OpCode::StopPid);
        case OpCode::ReachTarget:
            (void)instruction_temperature(instruction, temperature);
            state.cooling_to_target = false;
            if (std::fabs(temperature - observed.wort_temperature) > 0.2) {
                if (temperature > observed.wort_temperature) Z_RUNTIME_TRY(push(OpCode::WaitTempA));
                else { Z_RUNTIME_TRY(push(OpCode::WaitTempB)); state.cooling_to_target = true; }
            }
            Z_RUNTIME_TRY(push(OpCode::StartPid));
            Z_RUNTIME_TRY(push(OpCode::SetTarget, arg));
            Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x70));
            return push(OpCode::SubStepName, 0x8011);
        case OpCode::HoldFor:
            Z_RUNTIME_TRY(push(OpCode::WaitFor, arg)); return push(OpCode::SubStepName, 0x800b);
        case OpCode::CircFor:
            Z_RUNTIME_TRY(push(OpCode::WaitFor, arg));
            Z_RUNTIME_TRY(push(OpCode::MspPumps, state.flags & 0x20 ? 0x50 : 0x70));
            Z_RUNTIME_TRY(push(OpCode::StopPid)); return push(OpCode::SubStepName, 0x8010);
        case OpCode::DrainFor:
            Z_RUNTIME_TRY(push(OpCode::FlagClear, 0x1000));
            Z_RUNTIME_TRY(push(OpCode::MspPumps, requested_pumps | 0x40));
            Z_RUNTIME_TRY(push(OpCode::WaitFor, arg));
            Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x60));
            Z_RUNTIME_TRY(push(OpCode::StopPid));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x800e)); return push(OpCode::FlagSet, 0x1000);
        case OpCode::ShowScreen:
            Z_RUNTIME_TRY(push(OpCode::SetScreen)); Z_RUNTIME_TRY(push(OpCode::WaitUser, 1));
            return push(OpCode::SetScreen, arg);
        case OpCode::WaitDelay:
            if (!state.delay_seconds) return RuntimeResult::Ok;
            if (requested_valve) Z_RUNTIME_TRY(push(OpCode::MspValve, requested_valve));
            Z_RUNTIME_TRY(push(OpCode::WaitFor, state.delay_seconds));
            Z_RUNTIME_TRY(push(OpCode::MspValve, 3)); return push(OpCode::StepName, 0x8001);
        case OpCode::WaitTime: return push(OpCode::WaitFor, state.time_seconds);
        case OpCode::StopPidKD:
            Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40)); Z_RUNTIME_TRY(push(OpCode::SetTarget));
            return push(OpCode::StopPid);
        case OpCode::PrepareBrew:
            Z_RUNTIME_TRY(push(OpCode::CircFor, 60)); Z_RUNTIME_TRY(push(OpCode::MoveValve, 1));
            Z_RUNTIME_TRY(push(OpCode::StepName, 0x8003));
            if (!(state.flags & 4)) { Z_RUNTIME_TRY(push(OpCode::WaitDelay)); Z_RUNTIME_TRY(push(OpCode::GetDelay)); }
            Z_RUNTIME_TRY(push(OpCode::RequireFilter, 1)); return push(OpCode::ShowScreen, 11);
        case OpCode::CompleteBrew:
            Z_RUNTIME_TRY(push(OpCode::ShowScreen, (state.flags & 0x11) == 0x11 ? 24 : 14));
            Z_RUNTIME_TRY(push(OpCode::RequireFilter)); return push(OpCode::StopPidKD);
        case OpCode::StillStart:
            Z_RUNTIME_TRY(transition_still(2));
            if (!services.still_command) return RuntimeResult::MissingService;
            services.still_command(services.context, 3, 0xffffffffu); return RuntimeResult::Ok;
        case OpCode::StillScan:
            if (!services.still_command) return RuntimeResult::MissingService;
            services.still_command(services.context, 1, 0xffffffffu); return RuntimeResult::Ok;
        case OpCode::StSetTargetHL:
            temperature = observed.still_t3 + 3;
            // The original greater-than predicate selects 57 for unordered input.
            if (!(temperature > 57)) temperature = 57;
            return push(OpCode::SetTarget, static_cast<std::uint32_t>(temperature * 256));
        case OpCode::StFindHeatLoss:
            Z_RUNTIME_TRY(transition_still(3));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x8000));
            Z_RUNTIME_TRY(push(OpCode::WaitVacuum));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x8020));
            Z_RUNTIME_TRY(push(OpCode::WaitT3Stable));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x801e));
            Z_RUNTIME_TRY(push(OpCode::WaitFor, 180));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x801d));
            Z_RUNTIME_TRY(push(OpCode::WaitTempC));
            Z_RUNTIME_TRY(push(OpCode::StartPid));
            Z_RUNTIME_TRY(push(OpCode::StSetTargetHL));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x8011));
            Z_RUNTIME_TRY(push(OpCode::WaitFor, 60));
            Z_RUNTIME_TRY(push(OpCode::SubStepName, 0x801c));
            Z_RUNTIME_TRY(push(OpCode::SetPumps, 1)); return push(OpCode::StepName, 0x8018);
        case OpCode::StFindBoilPoint:
            Z_RUNTIME_TRY(transition_still(4));
            Z_RUNTIME_TRY(push(OpCode::WaitT1Rise)); return push(OpCode::StepName, 0x8019);
        case OpCode::StStabilize:
            Z_RUNTIME_TRY(transition_still(5));
            Z_RUNTIME_TRY(push(OpCode::WaitT3Drop));
            Z_RUNTIME_TRY(push(OpCode::SetPumps));
            Z_RUNTIME_TRY(push(OpCode::StopPid)); return push(OpCode::StepName, 0x801a);
        case OpCode::StDistille:
            Z_RUNTIME_TRY(transition_still(6));
            Z_RUNTIME_TRY(push(OpCode::WaitFor, 0xd2f00));
            Z_RUNTIME_TRY(push(OpCode::ShowScreen, 74));
            Z_RUNTIME_TRY(push(OpCode::StartPid));
            Z_RUNTIME_TRY(push(OpCode::SetPumps, 1)); return push(OpCode::StepName, 0x801b);
        case OpCode::RinseCycle: {
            Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
            static const std::uint32_t valves[] = {9, 8, 7, 6, 5, 4, 2};
            for (std::uint32_t valve : valves) {
                Z_RUNTIME_TRY(push(OpCode::WaitFor, 10));
                Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x70));
                Z_RUNTIME_TRY(push(OpCode::MspValve, valve));
                Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x60));
            }
            return RuntimeResult::Ok;
        }
        case OpCode::Setup:
            state.flags |= arg & 0xfff;
            if (arg & 0x400) {
                Z_RUNTIME_TRY(push(OpCode::ValveMode, arg & 3));
                return push(OpCode::StepName, 0x8022);
            }
            Z_RUNTIME_TRY(push(OpCode::PrepareBrew));
            Z_RUNTIME_TRY(push(OpCode::ValveMode, arg & 3));
            switch (arg & 0x300) {
                case 0x200: Z_RUNTIME_TRY(push(OpCode::ShowScreen, 10)); break;
                case 0x100: Z_RUNTIME_TRY(push_setup_instructions(state.flags)); break;
                case 0: break;
                default: Z_RUNTIME_TRY(record_assertion("Runtime.h",0x463,"setupMode == RSFLAG_INSTRUCTION_OFF",RuntimeResult::InvalidSetupMode));break;
            }
            if (arg & 0x800) Z_RUNTIME_TRY(push(OpCode::ShowScreen, 99));
            return push(OpCode::StepName, 0x8002);
        default: break;
    }
    return RuntimeResult::Ok;
}

// 0x400f46e8; output shutdown and controller transactions retain OEM order.
RuntimeResult ProgramRuntime::transition(RuntimeStateId id) {
    const RuntimeStateId previous = state.id;
    if (id == previous) return RuntimeResult::Ok;
    if (!services.millis) return RuntimeResult::MissingService;
    state.id = id;
    state.state_date = services.millis(services.context);
    switch (id) {
        case RuntimeStateId::Booting:
            Z_RUNTIME_TRY(move_valve(3));
            break;
        case RuntimeStateId::Idle: {
            if (!msp || !msp->state) return RuntimeResult::MissingService;
            if (msp->state->valve_position != 3) {
                Z_RUNTIME_TRY(record_assertion("Runtime.h",0x2fd,"_cState.mspState.valvePosition == POSITION_REST",RuntimeResult::InvalidValveLocation));
            }
            Z_RUNTIME_TRY(stop_outputs());
            if (!services.still_cancel) return RuntimeResult::MissingService;
            services.still_cancel(services.context);
            Z_RUNTIME_TRY(reset_t1_drop_watch());
            Z_RUNTIME_TRY(set_pumps(0x40));
            controller_result = z_msp_send(msp, Z_MSP_FAN_ON, 0, 0);
            if (controller_result != Z_MSP_OK) return RuntimeResult::ControllerFailure;
            Z_RUNTIME_TRY(enable_heater(false));
            Z_RUNTIME_TRY(set_target(0));
            stack.clear();
            // 0x400ef038 resets the represented fields but preserves id/date
            // and, unusually, does not clear StillState.pausedId (+0xd4).
            const std::uint32_t date = state.state_date;
            const std::uint32_t paused_still = state.still.paused_id;
            const TemperatureDropWatch watch = state.t1_drop_watch;
            state = ProgramRuntimeState{};
            state.id = id;
            state.state_date = date;
            state.still.paused_id = paused_still;
            state.t1_drop_watch = watch;
            requested_pumps = 0x40;
            requested_valve = 0;
            alternative_valve_map = false;
            filter_required = false;
            break;
        }
        case RuntimeStateId::Running:
            if (!state.ignore_monitor_safeties) {
                Z_RUNTIME_TRY(enable_heater(true));
            }
            break;
        case RuntimeStateId::Standby:
            if (!msp || !msp->state) return RuntimeResult::MissingService;
            if (msp->state->valve_position != 3) {
                Z_RUNTIME_TRY(record_assertion("Runtime.h",0x2f3,"_cState.mspState.valvePosition == POSITION_REST",RuntimeResult::InvalidValveLocation));
            }
            Z_RUNTIME_TRY(set_pumps(0));
            controller_result = z_msp_send(msp, Z_MSP_FAN_OFF, 0, 0);
            if (controller_result != Z_MSP_OK) return RuntimeResult::ControllerFailure;
            requested_valve = 0;
            requested_pumps = 0;
            saved_step_reference = 0;
            saved_substep_reference = 0;
            break;
        default: break; // Off and unrecognized values only emit the transition.
    }
    if (!services.event) return RuntimeResult::MissingService;
    services.event(services.context, RuntimeEventType::Transition,
                   static_cast<std::uint32_t>(previous), static_cast<std::uint32_t>(id));
    return RuntimeResult::Ok;
}

// 0x400e6240. The subtraction is the OEM unsigned tick arithmetic; a fully
// elapsed WaitFor asserts and then keeps the original unsigned subtraction.
RuntimeResult ProgramRuntime::save_pending(std::uint32_t now) {
    Instruction pending = state.instruction;
    if (pending.opcode == OpCode::WaitFor) {
        const std::uint32_t elapsed = (now - state.instruction_date) / 1000;
        if (pending.argument <= elapsed) {
            Z_RUNTIME_TRY(record_assertion("Runtime.h",0x1cc,"pending.arg > ellapsedSec",RuntimeResult::InvalidPendingInstruction));
        }
        pending.argument -= elapsed;
    }
    if (!instruction_valid(pending)) {
        Z_RUNTIME_TRY(record_assertion("Runtime.h",0x4e,"instr.isValid()",RuntimeResult::InvalidProgram));
    }
    return push(pending.opcode, pending.argument);
}

// 0x400e62d8: stack entries are appended in reverse execution order.
RuntimeResult ProgramRuntime::push_resume(std::uint32_t now, bool pending_completed) {
    if (!pending_completed) Z_RUNTIME_TRY(save_pending(now));
    if (state.screen) Z_RUNTIME_TRY(push(OpCode::SetScreen, state.screen));
    if (state.pid_active) Z_RUNTIME_TRY(push(OpCode::StartPid));
    Z_RUNTIME_TRY(push(OpCode::MspPumps, requested_pumps | 0x40));
    if (requested_valve) {
        Z_RUNTIME_TRY(push(OpCode::MspValve, requested_valve));
        Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
    }
    if (saved_substep_reference) Z_RUNTIME_TRY(push(OpCode::SubStepName, saved_substep_reference));
    if (saved_step_reference) Z_RUNTIME_TRY(push(OpCode::StepName, saved_step_reference));
    return RuntimeResult::Ok;
}

// 0x400e6bf8 plus the original 0x401bf29c skip predicate.
RuntimeResult ProgramRuntime::pending_complete(RuntimeCommandId command, std::uint32_t now, bool& result) {
    result = false;
    if (now < state.instruction_date) {
        Z_RUNTIME_TRY(record_assertion("Runtime.h",0x29e,"now >= _rtState.iStartDate",RuntimeResult::ClockWentBackwards));
    }
    switch (state.instruction.opcode) {
        case OpCode::WaitUser:
            if (state.pause_reason == 3) result = !observed.filter_absent;
            else if (command == RuntimeCommandId::Resume) result = true;
            else if (state.pause_reason == 12) result = !observed.pico_still_error;
            break;
        case OpCode::WaitTempA: result = observed.heater_target <= observed.wort_temperature + 0.2; break;
        case OpCode::WaitTempB: result = observed.heater_target >= observed.wort_temperature - 0.2; break;
        case OpCode::WaitTempC: result = std::fabs(observed.heater_target - observed.wort_temperature) <= 0.2; break;
        case OpCode::WaitTempE: result = std::fabs(observed.wort_temperature - observed.hex_temperature) <= 1.0; break;
        case OpCode::MspValve:
            if (!msp || !msp->state) return RuntimeResult::MissingService;
            result = !msp->state->moving_valve; break;
        case OpCode::GetDelay:
        case OpCode::GetTime: result = command == RuntimeCommandId::SetDuration; break;
        case OpCode::GetStepParams: result = command == RuntimeCommandId::StartManualStep; break;
        case OpCode::GetTemp: result = command == RuntimeCommandId::SetTemperature; break;
        case OpCode::WaitSealed:
            result = observed.signal_a00_defined && observed.scalar_a28 < 12 && observed.scalar_aa8 < -2;
            break;
        case OpCode::WaitVacuum:
            result = observed.signal_ab0_defined && observed.scalar_ad8 < 4 && std::fabs(observed.scalar_b58) < 0.2;
            break;
        case OpCode::WaitT3Stable:
            result = observed.signal_ab0_defined && std::fabs(observed.scalar_b48) < 0.2; break;
        case OpCode::WaitT1Rise:
            result = observed.signal_950_defined && observed.scalar_9d8 > 6 && observed.scalar_ab8 + 1 < observed.scalar_958;
            break;
        case OpCode::WaitT3Drop:
            result = observed.signal_950_defined && observed.scalar_a98 <= 0 &&
                     observed.wort_temperature - 0.2 <= state.still.boil_target;
            break;
        case OpCode::WaitT2Safety: result = observed.scalar_960 < 32.2; break;
        case OpCode::WaitPumpOk:
            result = !(observed.still_sample_present && !observed.byte_980 && observed.scalar_978 < 10);
            break;
        case OpCode::WaitStillReady:
            if (!services.still_ready) return RuntimeResult::MissingService;
            result = services.still_ready(services.context); break;
        case OpCode::HoldFor:
        case OpCode::CircFor:
        case OpCode::DrainFor: result = true; break;
        default:
            result = instruction_duration_ms(state.instruction) + state.instruction_date < now;
            break;
    }
    if (command == RuntimeCommandId::Skip) {
        const std::uint32_t opcode = static_cast<std::uint32_t>(state.instruction.opcode);
        if (opcode == 13 || opcode - 15u < 4 || opcode - 49u < 5) result = true;
    }
    return RuntimeResult::Ok;
}

// 0x400e6364: retain the original pending instruction and display references.
RuntimeResult ProgramRuntime::pause_still(std::uint32_t now, bool pending_completed,
                                         OpCode wait, std::uint32_t step_reference, bool stop_pumps) {
    if (state.still.paused_id) {
        Z_RUNTIME_TRY(record_assertion("Runtime.h",0x1ee,"_rtState.stillState.pausedId == RuntimeStillStateId::None",RuntimeResult::InvalidRuntimeState));
    }
    state.still.paused_id = state.still.id;
    if (!pending_completed) Z_RUNTIME_TRY(save_pending(now));
    if (stop_pumps) {
        if (state.pid_active) Z_RUNTIME_TRY(push(OpCode::StartPid));
        Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x50));
    }
    if (saved_substep_reference) Z_RUNTIME_TRY(push(OpCode::SubStepName, saved_substep_reference));
    if (saved_step_reference) Z_RUNTIME_TRY(push(OpCode::StepName, saved_step_reference));
    Z_RUNTIME_TRY(push(wait));
    if (stop_pumps) {
        Z_RUNTIME_TRY(push(OpCode::MspPumps, 0x40));
        if (state.pid_active) Z_RUNTIME_TRY(push(OpCode::StopPid));
    }
    return push(OpCode::StepName, step_reference);
}

// 0x400e660c. A Noop completed value means the current instruction is pending.
RuntimeResult ProgramRuntime::progress_still(std::uint32_t now, OpCode completed, bool& interrupted) {
    interrupted = false;
    const bool done = completed != OpCode::Noop;
    if (done && completed != state.instruction.opcode) {
        Z_RUNTIME_TRY(record_assertion("Runtime.h",0x23b,"!pendingCompleted || completed == _rtState.instruction.opCode",RuntimeResult::InvalidPendingInstruction));
    }
    const std::uint32_t id = state.still.id;
    if (id == 1) return RuntimeResult::Ok;
    if (id == 8) return completed == OpCode::WaitPumpOk ? transition_still(2) : RuntimeResult::Ok;
    if (!observed.still_sample_present) {
        if (id == 2) return RuntimeResult::Ok;
    } else {
        if (!observed.byte_980 && observed.scalar_978 < 10) {
            Z_RUNTIME_TRY(pause_still(now, done, OpCode::WaitPumpOk, 0x8023, id == 2));
            Z_RUNTIME_TRY(transition_still(8)); interrupted = true; return RuntimeResult::Ok;
        }
        if (id == 2) {
            if (!(observed.scalar_960 > 35)) return RuntimeResult::Ok;
            Z_RUNTIME_TRY(pause_still(now, done, OpCode::WaitT2Safety, 0x8022, true));
            Z_RUNTIME_TRY(transition_still(7)); interrupted = true; return RuntimeResult::Ok;
        }
    }
    if (id == 4) {
        if (completed != OpCode::WaitT1Rise) {
            if (!state.still.step_date) { Z_RUNTIME_TRY(record_assertion("RuntimeState.h",0x403,"isStepDefined()",RuntimeResult::UndefinedStillReference)); }
            if (state.still.step_date + 60000u < now) {
                if (!state.still.reference_date) { Z_RUNTIME_TRY(record_assertion("RuntimeState.h",0x41a,"isRefDefined()",RuntimeResult::UndefinedStillReference)); }
                state.still.step_target = (state.still.step_target + 0.5) - observed.scalar_b48;
                state.still.step_date = now;
            }
            return set_target(state.still.step_target + observed.still_t3);
        }
        if (!now) { Z_RUNTIME_TRY(record_assertion("Temperature.h",0x35,"now != 0",RuntimeResult::ClockWentBackwards)); }
        state.still.boil_t3 = observed.still_t3;
        state.still.boil_date = now;
        state.still.boil_scalar_978 = observed.scalar_978;
        state.still.boil_target = observed.still_t3 + state.still.heat_loss;
        Z_RUNTIME_TRY(set_target(state.still.boil_target));
        return transition_still(2);
    }
    if (id == 3 && completed == OpCode::WaitT3Stable) {
        state.still.reference_date = now;
        state.still.reference_wort = observed.wort_temperature;
        state.still.reference_t3 = observed.still_t3;
        state.still.heat_loss = observed.wort_temperature - observed.still_t3;
        return transition_still(2);
    }
    if (id == 7 && completed == OpCode::WaitT2Safety) return transition_still(2);
    return RuntimeResult::Ok;
}

// 0x400e6124. Fans may stop only for an inactive-pump WaitFor at/below threshold.
RuntimeResult ProgramRuntime::update_fan(double threshold) {
    if (state.id != RuntimeStateId::Running) { Z_RUNTIME_TRY(record_assertion("Runtime.h",0x4eb,"RuntimeStateId::Running == _rtState.stateId",RuntimeResult::InvalidRuntimeState)); }
    if (!msp || !msp->state) return RuntimeResult::MissingService;
    const bool off = state.instruction.opcode == OpCode::WaitFor && !(msp->state->pumps & 0x30) &&
                     threshold >= observed.wort_temperature && threshold >= observed.hex_temperature &&
                     threshold >= observed.drain_temperature;
    if ((msp->state->fan != 0) == !off) return RuntimeResult::Ok;
    controller_result = z_msp_send(msp, off ? Z_MSP_FAN_OFF : Z_MSP_FAN_ON, 0, 0);
    return controller_result == Z_MSP_OK ? RuntimeResult::Ok : RuntimeResult::ControllerFailure;
}

#undef Z_RUNTIME_TRY
}} // namespace zseries::oem
