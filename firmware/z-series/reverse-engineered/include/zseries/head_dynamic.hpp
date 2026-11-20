// Sensor snapshots and concrete recurring Head painters.
#pragma once
#include "zseries/head_widgets.hpp"
#include "zseries/core_commands.hpp"
#include "zseries/program_runtime.hpp"
#include "zseries/heater.hpp"
#include "zseries/auxiliary_sensors.hpp"
#include "zseries/safeties.hpp"
#include "zseries/pico_still.hpp"
#include "zseries/pico_still_sensors.hpp"
namespace zseries { namespace oem {
// Owned values refreshed by Core at the original Head stage. Program strings
// retain their references into the owning runtime/program repository.
struct HeadDynamicObserved {
    CoreFlowState core;
    ProgramRuntimeState runtime;
    RuntimeObserved runtime_inputs;
    CoreSession session;
    WifiState wifi;
    HeaterState heater;
    HeaterSensors heater_sensors;
    AuxiliarySensors auxiliary;
    SafetyState safety;
    z_msp_state msp{};
    PicoStillState still;
    PicoStillSamples still_samples;
    std::uint32_t network=0;
    float reference_pressure_pa=101325.0f;
    bool imperial=false,still_connectivity_error=false;
};
bool head_instruction_argument_text(char*,std::size_t,const Instruction&);
bool head_instruction_text(char*,std::size_t,const Instruction&);
void head_duration_text(char*,std::size_t,std::uint32_t seconds,bool omit_zero_hours=false);
class HeadDynamicPainter {
public:
    HeadDynamicPainter(HeadWidgetPainter& output,const HeadDynamicObserved& values)noexcept:painter(output),observed(values){}
    HeadWidgetPainter& painter;
    const HeadDynamicObserved& observed;
    const DisplayTextContext* text_context=nullptr; // Level uses the current display context.
    HeadWidgetResult runtime_state(); // 0x400f96f4
    HeadWidgetResult runtime_info(const DisplayRect&); // 0x400e89c4
    HeadWidgetResult pause(const DisplayRect&,PauseReason); // 0x40102788 body
    HeadWidgetResult level(); // 0x400e905c
private:
    HeadWidgetResult line(const DisplayRect&,DisplayFontId,const char*,std::int32_t row=0,DisplayTextAlignment alignment=DisplayTextAlignment::Center,std::int32_t offset_y=0);
    HeadWidgetResult assertion(std::uint32_t);
};
}} // namespace zseries::oem
