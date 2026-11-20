#include "zseries/safeties.hpp"
#include <cstdint>
using namespace zseries::oem;
namespace {
struct Input { int value; unsigned reads; };
int read(void* p,std::uint8_t pin) { auto& in=*static_cast<Input*>(p);if(pin!=36)return -1;++in.reads;return in.value; }
}
extern "C" int safeties_action(double* values,std::uint32_t* words,std::uint32_t* pin_reads,int missing_io) {
    HeaterSensors sensors;
    sensors.temperatures[0].celsius=values[0];sensors.temperatures[1].celsius=values[1];
    sensors.temperatures[2].celsius=values[2];sensors.hex_rate=values[3];
    SafetyController controller;
    controller.hex_rise.threshold=values[4];controller.hex_wort_delta.threshold=values[5];controller.wort_drain_delta.threshold=values[6];
    SafetyState state;
    state.thermal.enabled=words[0]!=0;state.thermal.too_hot=words[1];state.thermal.too_cold=words[2];state.thermal.too_old=words[3];
    state.max_hex_wort_delta=words[4]!=0;state.max_wort_drain_delta=words[5]!=0;state.max_hex_rise=words[6]!=0;state.filter_missing=words[7]!=0;
    controller.previous_hot=words[8];controller.previous_cold=words[9];controller.previous_old=words[10];
    controller.hex_rise.enabled=words[11]!=0;controller.hex_rise.deadline_ms=words[12];
    controller.hex_wort_delta.enabled=words[13]!=0;controller.hex_wort_delta.deadline_ms=words[14];
    controller.wort_drain_delta.enabled=words[15]!=0;controller.wort_drain_delta.deadline_ms=words[16];
    z_msp_state msp={};msp.pumps=words[17];msp.valve_position=words[18];
    Input input={static_cast<int>(words[19]),0};SafetyIo io={&input,nullptr,missing_io?nullptr:read};
    const bool result=update_safeties(controller,state,sensors,msp,words[20],io);
    values[4]=controller.hex_rise.threshold;values[5]=controller.hex_wort_delta.threshold;values[6]=controller.wort_drain_delta.threshold;
    words[0]=state.thermal.enabled;words[1]=state.thermal.too_hot;words[2]=state.thermal.too_cold;words[3]=state.thermal.too_old;
    words[4]=state.max_hex_wort_delta;words[5]=state.max_wort_drain_delta;words[6]=state.max_hex_rise;words[7]=state.filter_missing;
    words[8]=controller.previous_hot;words[9]=controller.previous_cold;words[10]=controller.previous_old;
    words[11]=controller.hex_rise.enabled;words[12]=controller.hex_rise.deadline_ms;
    words[13]=controller.hex_wort_delta.enabled;words[14]=controller.hex_wort_delta.deadline_ms;
    words[15]=controller.wort_drain_delta.enabled;words[16]=controller.wort_drain_delta.deadline_ms;
    *pin_reads=input.reads;return result;
}
