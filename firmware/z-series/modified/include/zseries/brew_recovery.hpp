#pragma once
#include "picobrew/device_services.hpp"
#include "zseries/program_repository.hpp"

namespace zseries { namespace standalone {
namespace api = picobrew::standalone;

// A verified value snapshot. It contains no borrowed strings, pointers, network
// credentials or hardware callbacks. Loading it cannot operate the controller.
class BrewRecovery {
public:
    api::Result load(api::Storage);
    api::Result prepare(const api::Recipe&, const oem::ProgramBuffer&);
    api::Result capture(const oem::ProgramRuntime&, double target_celsius,
                        std::uint32_t now, std::uint32_t previous_elapsed);
    api::Result restore(oem::ProgramBuffer&, double pressure_pascal) const;
    api::Result clear();
    bool ready() const noexcept { return ready_; }
    bool present() const noexcept { return present_; }
    const api::Recipe& recipe() const noexcept { return saved_.recipe; }
    std::uint32_t elapsed() const noexcept { return saved_.elapsed; }
    void describe(api::Recovery&, bool available) const noexcept;
private:
    struct Checkpoint {
        api::Recipe recipe;
        std::array<oem::Instruction,oem::InstructionStack::capacity> code{};
        std::size_t count = 0;
        std::uint32_t elapsed = 0, seconds = 0;
        std::int32_t step = -1;
        char step_name[32] = {};
    } saved_;
    api::Storage storage_;
    std::uint32_t generation_ = 0;
    bool ready_ = false, present_ = false;
    api::Result save(const Checkpoint&, bool present);
};
}}
