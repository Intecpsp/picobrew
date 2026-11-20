// Concrete incoming HTTP routes bound to recovered machine and recipe state.
#pragma once
#include "zseries/local_http_state.hpp"
#include "zseries/program_list.hpp"
#include "zseries/auxiliary_sensors.hpp"

namespace zseries { namespace oem {
LocalHttpResult route_local_http_post(LocalHttpServer&, ProgramBuffer&,
    const RecipeEnvironment&, LocalRecipePublication&);

class LocalHttpApplication {
public:
    LocalHttpApplication(LocalHttpServer& server, ProgramBuffer& program,
        LocalRecipePublication& publication, const HeaterSensors& sensors,
        const HeaterController& heater, const AuxiliarySensors& auxiliary,
        const z_msp_state& msp, const ProgramRuntime& runtime, const std::int32_t& rssi) noexcept;
    // Installs actual state serialization, recipe parsing/publication and
    // response output. The transport is bound independently by the SDK adapter.
    void bind() noexcept;
    LocalHttpServer& server;
    ProgramBuffer& program;
    LocalRecipePublication& publication;
    const HeaterSensors& sensors;
    const HeaterController& heater;
    const AuxiliarySensors& auxiliary;
    const z_msp_state& msp;
    const ProgramRuntime& runtime;
    const std::int32_t& rssi;
};
}} // namespace zseries::oem
