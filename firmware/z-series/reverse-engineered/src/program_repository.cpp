#include "zseries/program_repository.hpp"
#include <algorithm>
#include <cstring>

namespace zseries { namespace oem {
ProgramRecord::ProgramRecord(SessionType session, std::uint32_t id, const char* name,
        const Instruction* code, std::size_t count, const char* const* strings, std::size_t strings_count) noexcept
    : type(session), unique_id(id) {
    view.name = name; view.code = code; view.code_size = count;
    view.strings = strings; view.string_count = strings_count;
}
// 0x401bf62c and the hasProgram predicate in 0x400dfd9c.
bool ProgramRecord::empty() const noexcept {
    return view.code_size == 0 && view.string_count == 0 && type == SessionType::Unknown;
}
bool ProgramRecord::has_program() const noexcept {
    return view.code_size != 0 && view.string_count != 0 && type != SessionType::Unknown;
}

// 0x400dfea0. Own all storage; no relocated OEM pointer remains in this object.
ProgramBuffer::ProgramBuffer() noexcept {
    for (std::size_t i = 0; i < string_capacity; ++i) string_table_[i] = strings_[i].data();
    record_.view.name = string_table_[0]; record_.view.strings = string_table_.data();
    record_.view.code = code_.data(); reset();
}
// 0x400dfe58 preserves the unique ID and unused code/string bytes.
void ProgramBuffer::reset() noexcept {
    record_.type = SessionType::Unknown;
    record_.view.code_size = 0; record_.view.string_count = 0;
    std::memcpy(strings_[0].data(), "<EMPTY>", 8);
    parameters = ProgramParameters{}; stats = ProgramStats{};
}
// 0x400d752c: a full buffer is unchanged; the OEM logs and continues its caller.
ProgramBuildResult ProgramBuffer::append(Instruction instruction) noexcept {
    if (record_.view.code_size == instruction_capacity) return ProgramBuildResult::CodeFull;
    code_[record_.view.code_size++] = instruction; return ProgramBuildResult::Ok;
}
// 0x400e0e84. Capacity is checked before the nonempty-string assertion.
ProgramBuildResult ProgramBuffer::append_string(const char* text) noexcept {
    if (record_.view.string_count == string_capacity) return ProgramBuildResult::StringsFull;
    if (!text || !*text) { assertion_line = 0x153; return ProgramBuildResult::EmptyString; }
    const std::size_t length = std::min(std::strlen(text), string_bytes - 1);
    auto& destination = strings_[record_.view.string_count];
    std::memcpy(destination.data(), text, length); destination[length] = 0;
    ++record_.view.string_count; return ProgramBuildResult::Ok;
}
// 0x400e0f18. An overlong name is copied/truncated before the equality assertion.
ProgramBuildResult ProgramBuffer::start(SessionType session, std::uint32_t id, const char* name) noexcept {
    reset(); record_.type = session; record_.unique_id = id;
    const ProgramBuildResult result = append_string(name);
    if (result != ProgramBuildResult::Ok) return result;
    if (std::strcmp(name, record_.view.name) != 0) { assertion_line = 0x272; return ProgramBuildResult::NameTruncated; }
    return ProgramBuildResult::Ok;
}
// 0x400e3e24. A null source is invalid even when the count is zero.
ProgramBuildResult ProgramBuffer::set_code(const Instruction* instructions, std::size_t count) noexcept {
    if (!instructions) { assertion_line = 0x16d; return ProgramBuildResult::NullCode; }
    if (count > instruction_capacity) { assertion_line = 0x16e; return ProgramBuildResult::TooManyInstructions; }
    std::copy(instructions, instructions + count, code_.begin());
    record_.view.code_size = count; return ProgramBuildResult::Ok;
}

// 0x401bf2cc. All five IDs share the same mutable program object.
bool is_buffered_program(std::uint32_t id) noexcept { return id >= 20 && id <= 24; }
// 0x400e0f5c, reference volume 11.36 L at 1 C/min, minimum 0.1 L.
bool program_heating_rate(double batch_liters, double& result) noexcept {
    if (!(batch_liters >= 0.1)) return false;
    result = 11.36 / batch_liters; return true;
}
// 0x400d7df8. Lookup does not validate the buffered program before returning it.
const ProgramRecord* ProgramRepository::find(std::uint32_t id) const noexcept {
    return is_buffered_program(id) ? &buffer_.record() : builtin_program(id);
}
const ProgramView* ProgramRepository::runtime_find(void* context, std::uint32_t id, double& heating_rate) noexcept {
    if (!context) return nullptr;
    const auto& repository = *static_cast<ProgramRepository*>(context);
    const ProgramRecord* program = repository.find(id);
    if (!program) return nullptr;
    // OEM Runtime reads this from the shared buffer even for built-in programs.
    heating_rate = repository.buffer_.stats.heating_rate_celsius_per_minute;
    return &program->view;
}
}} // namespace zseries::oem
