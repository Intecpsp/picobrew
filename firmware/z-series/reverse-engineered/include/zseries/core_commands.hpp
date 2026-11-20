// Original Core command dispatch and session/program-start metadata.
#pragma once
#include "zseries/core_flow.hpp"
#include "zseries/auxiliary_sensors.hpp"
#include "zseries/hex_detection.hpp"

namespace zseries { namespace oem {
struct CoreSession {
    std::uint32_t session_id = 0;
    std::uint32_t unique_program_id = 0xffffffffu;
    std::array<char, 65> name{};
    ProgramParameters parameters;
    ProgramStats stats;
    bool use_azure = true;
};

class CoreCommands {
public:
    CoreCommands(CoreFlow& core, ProgramRepository& programs, TemperatureAcquisition& acquisition,
                 HeaterController& heater, HeaterSensors& temperatures, AuxiliarySensors& auxiliary) noexcept
        : core(core), programs(programs), acquisition(acquisition), heater(heater), temperatures(temperatures), auxiliary(auxiliary) {}
    CoreFlow& core;
    ProgramRepository& programs;
    TemperatureAcquisition& acquisition;
    HeaterController& heater;
    HeaterSensors& temperatures;
    AuxiliarySensors& auxiliary;
    CoreSession session;
    // Original Core +0x17b0. Interpretation belongs to the backend/remote
    // program source; only the evidenced numeric comparison is made here.
    std::uint32_t program_source_17b0 = 0;
    bool resumed_session = false; // Original session logger +0x21.
    ProgramBuildResult last_program_result = ProgramBuildResult::Ok;

    CoreFlowResult dispatch(const CoreCommand&); // 0x4011d32c
    CoreFlowResult start_program(std::uint32_t id); // 0x4011d010
    CoreFlowResult load_coffee(const CoreCommand&); // 0x400f0480
    CoreFlowResult set_boiler(CoreCommandId); // 0x400ee254

private:
    CoreFlowResult restart();
    CoreFlowResult reset(bool factory);
    CoreFlowResult runtime_command(RuntimeCommandId, const CoreCommand* source = nullptr);
    CoreFlowResult delegate(CoreEffect, const CoreCommand&);
    CoreFlowResult connect_pending(const CoreCredentials&);
    CoreFlowResult use_offline();
};
}} // namespace zseries::oem
