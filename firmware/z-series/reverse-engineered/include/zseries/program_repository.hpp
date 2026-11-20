// Original Z119 Program / ProgramBuffer / ProgramWriter / ProgramResolver.
#pragma once
#include "zseries/program_runtime.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace zseries { namespace oem {
struct ProgramRecord {
    SessionType type = SessionType::Unknown;
    std::uint32_t unique_id = 0xffffffffu;
    ProgramView view;
    ProgramRecord() noexcept = default;
    ProgramRecord(SessionType session, std::uint32_t id, const char* name,
                  const Instruction* code, std::size_t count,
                  const char* const* strings, std::size_t strings_count) noexcept;
    bool empty() const noexcept;
    bool has_program() const noexcept;
};

struct ProgramParameters {
    ZProgramId id = ZProgramId::None;
    std::int32_t abv = -1;
    std::int32_t ibu = -1;
    Quantity time{0, Unit::None};
    Quantity water{0, Unit::None};
    Quantity intensity{0, Unit::None};
    Quantity temperature{0, Unit::None};
    bool resumed_session = false;
};
struct ProgramStats {
    double heating_rate_celsius_per_minute = 0;
    std::uint32_t duration_seconds = 0;
    double pressure_pascal = 0;
    double maximum_temperature_celsius = 0;
    std::uint32_t maximum_temperature_added_seconds = 0;
};

enum class ProgramBuildResult {
    Ok, CodeFull, StringsFull, EmptyString, NameTruncated, NullCode,
    TooManyInstructions, InvalidVolume, InvalidUnit, InvalidTemperature,
    InvalidNumericDomain, InvalidProgramId, TruncatedEncoding, InvalidEncoding
};

class ProgramBuffer {
public:
    static constexpr std::size_t instruction_capacity = 256;
    static constexpr std::size_t string_capacity = 64;
    static constexpr std::size_t string_bytes = 48;
    ProgramBuffer() noexcept;
    ProgramBuffer(const ProgramBuffer&) = delete;
    ProgramBuffer& operator=(const ProgramBuffer&) = delete;

    void reset() noexcept;
    ProgramBuildResult start(SessionType, std::uint32_t id, const char* name) noexcept;
    ProgramBuildResult append(Instruction) noexcept;
    ProgramBuildResult append_string(const char*) noexcept;
    ProgramBuildResult set_code(const Instruction*, std::size_t count) noexcept;
    const ProgramRecord& record() const noexcept { return record_; }
    const std::array<Instruction, instruction_capacity>& code_storage() const noexcept { return code_; }
    const std::array<std::array<char, string_bytes>, string_capacity>& string_storage() const noexcept { return strings_; }
    ProgramParameters parameters;
    ProgramStats stats;
    std::uint32_t assertion_line = 0;

private:
    ProgramRecord record_;
    std::array<Instruction, instruction_capacity> code_{};
    std::array<std::array<char, string_bytes>, string_capacity> strings_{};
    std::array<const char*, string_capacity> string_table_{};
};

const std::array<ProgramRecord, 23>& builtin_programs() noexcept;
const ProgramRecord* builtin_program(std::uint32_t id) noexcept;
bool is_buffered_program(std::uint32_t id) noexcept;
bool program_heating_rate(double batch_liters, double& result) noexcept;
ProgramBuildResult finish_program(ProgramBuffer&, const ProgramParameters&, double pressure_pascal,
    double maximum_temperature_celsius, std::uint32_t maximum_temperature_added_seconds) noexcept;
ProgramBuildResult begin_coffee_program(ProgramBuffer&, const char* name) noexcept;
void append_cold_coffee_steps(ProgramBuffer&) noexcept;
ProgramBuildResult append_hot_coffee_steps(ProgramBuffer&, std::int32_t fahrenheit, std::int32_t minutes) noexcept;
ProgramBuildResult coffee_intensity_minutes(const Quantity&, std::int32_t& result) noexcept;
ProgramBuildResult build_coffee_program(ProgramBuffer&, const ProgramParameters&, double pressure_pascal,
    bool imperial) noexcept;

// Runtime's resolver is Core +0x3cfc, pointing at the buffer at Core +0x17b8.
// It is distinct from the HTTP ZBackend at Core +0xfff0.
class ProgramRepository {
public:
    explicit ProgramRepository(ProgramBuffer& buffer) noexcept : buffer_(buffer) {}
    const ProgramRecord* find(std::uint32_t id) const noexcept;
    ProgramBuildResult static_stats(std::uint32_t id, double pressure_pascal,
        double estimated_boil_celsius, ProgramStats& result) const noexcept;
    ProgramBuffer& buffer() noexcept { return buffer_; }
    // Suitable for RuntimeServices::find_program when context is this repository.
    // Composite application contexts can delegate to find() and buffer().stats.
    static const ProgramView* runtime_find(void* context, std::uint32_t id, double& heating_rate) noexcept;
private:
    ProgramBuffer& buffer_;
};
}} // namespace zseries::oem
