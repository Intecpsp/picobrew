#include "zseries/runtime_instruction.hpp"

namespace zseries { namespace oem {

bool instruction_temperature(const Instruction& instruction, double& celsius) noexcept {
    switch (instruction.opcode) {
        case OpCode::ReachTarget:
        case OpCode::SetTarget:
        case OpCode::SetITMin:
        case OpCode::SetITMax:
            celsius = static_cast<double>(instruction.argument) * (1.0 / 256.0);
            return true;
        default: return false;
    }
}

bool instruction_valid(const Instruction& instruction) noexcept {
    const std::uint32_t argument = instruction.argument;
    switch (instruction.opcode) {
        case OpCode::Pause: return argument <= 17;
        case OpCode::StepName:
        case OpCode::SubStepName:
        case OpCode::WaitDelay:
        case OpCode::WaitTime:
        case OpCode::SetTime:
        case OpCode::SetITMin:
        case OpCode::SetITMax:
        case OpCode::Setup:
            return true;
        case OpCode::ReachTarget:
        case OpCode::SetTarget:
            // uint32 / 256 is exact in binary64; original bounds are 0..98 C.
            return argument <= 98U * 256U;
        case OpCode::HoldFor:
        case OpCode::CircFor:
        case OpCode::DrainFor:
        case OpCode::FlagSet:
        case OpCode::FlagClear:
        case OpCode::WaitFor:
            return argument != 0;
        case OpCode::MoveValve: return argument - 1U < 6U;
        case OpCode::SetPumps:
            // Original slli 4 followed by AND 0xffffff8f. Bits 28..31 shift
            // out on the 32-bit CPU, so preserve that behavior explicitly.
            return ((argument << 4) & 0xffffff8fU) == 0;
        case OpCode::WaitUser: return argument - 1U <= 16U;
        case OpCode::MspValve: return argument - 1U <= 8U;
        case OpCode::MspPumps: return (argument & 0xffffffcfU) == 0x40U;
        case OpCode::ValveMode:
        case OpCode::Heater:
        case OpCode::RequireFilter:
            return argument < 2;
        case OpCode::SetScreen:
        case OpCode::ShowScreen:
            return argument <= 102;
        case OpCode::NoTarget:
        case OpCode::Noop:
        case OpCode::WaitTempA:
        case OpCode::WaitTempB:
        case OpCode::WaitTempC:
        case OpCode::WaitTempE:
        case OpCode::GetDelay:
        case OpCode::GetStepParams:
        case OpCode::GetTemp:
        case OpCode::GetTime:
        case OpCode::StartPid:
        case OpCode::StopPid:
        case OpCode::StopPidKD:
        case OpCode::PrepareBrew:
        case OpCode::CompleteBrew:
        case OpCode::StillScan:
        case OpCode::StillStart:
        case OpCode::StSetTargetHL:
        case OpCode::StFindHeatLoss:
        case OpCode::StFindBoilPoint:
        case OpCode::StStabilize:
        case OpCode::StDistille:
        case OpCode::WaitStillReady:
        case OpCode::WaitSealed:
        case OpCode::WaitVacuum:
        case OpCode::WaitT3Stable:
        case OpCode::WaitT1Rise:
        case OpCode::WaitT3Drop:
        case OpCode::WaitT2Safety:
        case OpCode::WaitPumpOk:
        case OpCode::RinseCycle:
            return argument == 0;
        default: return false;
    }
}

bool is_common_string_reference(std::uint32_t reference) noexcept {
    return reference - 0x8000U <= 0x25U;
}

}} // namespace zseries::oem
