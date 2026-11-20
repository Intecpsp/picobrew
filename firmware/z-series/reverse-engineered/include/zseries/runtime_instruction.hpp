// Recovered Z119 program instruction representation and validation.
#pragma once
#include "zseries/enums.hpp"
#include <cstdint>

namespace zseries { namespace oem {

struct Instruction {
    OpCode opcode;
    std::uint32_t argument;
};
static_assert(sizeof(Instruction) == 8, "OEM instruction has two 32-bit words");

// Exact predicate from 0x400dff64. This preserves the original treatment of
// unused arguments and high pump-argument bits; it is not a replacement for
// command authorization, thermal interlocks, or external-input validation.
bool instruction_valid(const Instruction& instruction) noexcept;

// Original 0x400dff0c: unsigned fixed point with eight fractional bits.
// Only ReachTarget, SetTarget, SetITMin and SetITMax have this argument kind.
// An unsupported opcode is rejected without changing the output.
bool instruction_temperature(const Instruction& instruction, double& celsius) noexcept;

// Original range guard in 0x400e0fb4, also named by the embedded OEM test.
bool is_common_string_reference(std::uint32_t reference) noexcept;

}} // namespace zseries::oem
