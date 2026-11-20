#include "zseries/serial_logger.hpp"
#include <cmath>

namespace zseries { namespace oem {
namespace {
const char* const pump_names[]={"---","--K","-D-","-DK","H--","H-K","HD-","HDK",
    "????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  "};
// Pumps upper nibbles 8..15 read the adjacent valve table in the OEM image.
const char* const valve_names[]={"????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  ","ZP3 ","ZP4 "};
double deviation(const PicoStillStatistics& statistics){
    return std::sqrt(statistics.count>1?statistics.squared_deviations/static_cast<double>(statistics.count-1):0.0);
}
}
SerialLogResult core_csv_header(void*,CsvBuffer& buffer,const SerialLogServices& services){
    const char* fields[]={"wTarget,tbDuty,wPid,pidITerm",
        "Wort0p5Hz,WortSpeed,Hex0p5Hz,HexSpeed,Wort,Hex,Drain,Ambient,Pressure,Altitude,BoilTemp",
        "MspError,Fans,Pumps,PumpsText,Valve,ValveText,Stepper,StateFlags",
        "TotalLeft,iLeft,iText,iArg,StepIndex,StepName",
        "t1,t2,t3,t4,p,pdev,t1dev,t2dev"};
    for(unsigned i=0;i<5;++i){if(i){const auto r=buffer.append(",",services);if(r!=SerialLogResult::Ok)return r;}
        const auto r=buffer.append(fields[i],services);if(r!=SerialLogResult::Ok)return r;}
    return SerialLogResult::Ok;
}
SerialLogResult core_csv_data(void* context,CsvBuffer& b,const SerialLogServices& s){
    if(!context)return SerialLogResult::MissingService;
    const auto& source=*static_cast<const CoreCsvSources*>(context);
    if(!source.heater||!source.temperatures||!source.auxiliary||!source.msp||!source.runtime||!source.still)return SerialLogResult::MissingService;
    const auto& h=*source.heater;const auto& t=*source.temperatures;const auto& m=*source.msp;
    const auto& runtime=*source.runtime;
    auto r=b.format(s,"%.1f,%.1f,%s,%.1f",h.target_celsius,h.output_ticks,h.started?"true":"false",h.integral_display);
    if(r!=SerialLogResult::Ok)return r;
    r=b.append(",",s);if(r!=SerialLogResult::Ok)return r;
    const auto altitude=(1.0-std::pow(source.auxiliary->pressure_pa/static_cast<double>(source.reference_pressure_pa),0.1903))*44330.0;
    r=b.format(s,"%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,%3.2fC",
        t.previous_wort,t.wort_rate,t.previous_hex,t.hex_rate,
        t.temperatures[static_cast<std::size_t>(TempSensorId::Wort)].celsius,
        t.temperatures[static_cast<std::size_t>(TempSensorId::Hex)].celsius,
        t.temperatures[static_cast<std::size_t>(TempSensorId::Drain)].celsius,
        t.temperatures[static_cast<std::size_t>(TempSensorId::Ambient)].celsius,
        source.auxiliary->pressure_pa,altitude,t.estimated_boil_celsius);
    if(r!=SerialLogResult::Ok)return r;
    r=b.append(",",s);if(r!=SerialLogResult::Ok)return r;
    r=b.format(s,"0x%x,0x%x,0x%x,%s,%x,%s,%d,%x",static_cast<unsigned>(m.errors),
        static_cast<unsigned>(m.fan),static_cast<unsigned>(m.pumps),pump_names[m.pumps>>4],
        static_cast<unsigned>(m.valve_position),m.moving_valve?"****":valve_names[m.valve_position>=1&&m.valve_position<=9?m.valve_position:0],
        static_cast<int>(m.status_byte_2),static_cast<unsigned>(m.status_byte_3));
    if(r!=SerialLogResult::Ok)return r;
    r=b.append(",",s);if(r!=SerialLogResult::Ok)return r;
    r=b.format(s,"%d,%d,%s,%d,%d,%s",static_cast<int>(static_cast<std::int32_t>(runtime.total_left_seconds)),
        static_cast<int>(static_cast<std::int32_t>(runtime.instruction_left_seconds)),to_string(runtime.instruction.opcode),
        static_cast<int>(runtime.instruction.argument),static_cast<int>(static_cast<std::int32_t>(runtime.step_count)),runtime.step_name);
    if(r!=SerialLogResult::Ok)return r;
    r=b.append(",",s);if(r!=SerialLogResult::Ok)return r;
    return still_csv_data(*source.still,b,s);
}
SerialLogResult still_csv_data(const PicoStillSamples& still,CsvBuffer& b,const SerialLogServices& s){
    return b.format(s,"%.1f,%.1f,%.1f,%.1f,%.2f,%.5f,%.5f,%.5f",still.current.temperature[0],still.current.temperature[1],
        still.current.temperature[2],still.current.temperature[3],still.current.pressure,
        deviation(still.pressure),deviation(still.t1),deviation(still.t2));
}
CsvSource core_csv_source(CoreCsvSources& state) noexcept {CsvSource source;source.context=&state;source.header=core_csv_header;source.data=core_csv_data;return source;}
}} // namespace zseries::oem
