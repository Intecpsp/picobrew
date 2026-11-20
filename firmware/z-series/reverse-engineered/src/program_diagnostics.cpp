#include "zseries/program_diagnostics.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdarg>
namespace zseries { namespace oem {
namespace {
void duration_text(char* output,std::size_t size,std::uint32_t seconds){
    seconds=std::min<std::uint32_t>(3599999,seconds);
    ::snprintf(output,size,"%u:%02u:%02u",seconds/3600,seconds%3600/60,seconds%60);
}
bool argument_text(char* output,std::size_t size,const Instruction& instruction){
    static const char* const pumps[]={"---","--K","-D-","-DK","H--","H-K","HD-","HDK"};
    static const char* const valves[]={"????","Home","Pass","Rest","Mash","Z1  ","Z2P1","P2  ","ZP3 ","ZP4 "};
    static const char* const locations[]={"????","Pass","Mash","Adj1","Adj2","Adj3","Adj4"};
    const auto op=static_cast<std::uint32_t>(instruction.opcode),value=instruction.argument;
    if(op==27)::snprintf(output,size,"%s",valves[static_cast<std::uint8_t>(value)-1u<9u?static_cast<std::uint8_t>(value):0]);
    else if(op==6){if(value>=7)return false;::snprintf(output,size,"%s",locations[value]);}
    else if(op==28||op==7){const auto index=op==28?(value&255u)>>4:value&15u;if(index>=8)return false;::snprintf(output,size,"%s",pumps[index]);}
    else if(op==29)::snprintf(output,size,"%s",value==1?"PPK":"ZYM");
    else if(op==25||op==2||op-35u<2u){const double value_c=value/256.0;::snprintf(output,size,"%3.1fC %3.1fF",value_c,value_c*1.8+32);}
    else if(op<27&&(0x5002038u&(1u<<op)))duration_text(output,size,value);
    else if(op-30u<2u)::snprintf(output,size,"%s",to_string(static_cast<ScreenId>(value)));
    else ::snprintf(output,size,"0x%04x",value);
    return true;
}
bool log(const ConsoleServices& services,const char* tag,const char* file,std::uint32_t line,const char* format,...){
    if(!services.log)return false;
    char text[512];va_list args;va_start(args,format);const int length=::vsnprintf(text,sizeof text,format,args);va_end(args);
    return length>=0&&static_cast<std::size_t>(length)<sizeof text&&services.log(services.context,ConsoleLevel::Info,tag,file,line,text);
}
}
bool report_program_instruction(const ConsoleServices& services,const Instruction& instruction){
    char op[48],argument[32];if(!argument_text(argument,sizeof argument,instruction))return false;
    ::snprintf(op,sizeof op,"OpCode::%s,",to_string(instruction.opcode));
    return log(services,"PGM","RuntimeState.h",0x332,"{ %-22s 0x%04x }, // %s",op,instruction.argument,argument);
}
bool report_program_record(const ConsoleServices& services,const ProgramRecord& record){
    const auto& view=record.view;
    if(!view.name||(!view.code&&view.code_size)||(!view.strings&&view.string_count))return false;
    if(!log(services,"PGM",nullptr,0,"sessionType = %s",to_string(record.type))||
       !log(services,"PGM",nullptr,0,"recipeId = %d",static_cast<std::int32_t>(record.unique_id))||
       !log(services,"PGM",nullptr,0,"name = %s",view.name)||
       !log(services,"PGM",nullptr,0,"codeSize = %u",static_cast<unsigned>(view.code_size))||
       !log(services,"PGM",nullptr,0,"stringsCount = %u",static_cast<unsigned>(view.string_count)))return false;
    for(std::size_t i=0;i<view.code_size;++i)if(!report_program_instruction(services,view.code[i]))return false;
    for(std::size_t i=0;i<view.string_count;++i)if(!view.strings[i]||!log(services,"PGM",nullptr,0,"str = %s",view.strings[i]))return false;
    return true;
}
bool report_program_parameters(const ConsoleServices& services,const ProgramParameters& parameters){
    if(!log(services,"PPRMS","ProgramParams.h",0x8d,"Id = %s",to_string(parameters.id))||
       !log(services,"PPRMS","ProgramParams.h",0x8e,"ABV = %d",parameters.abv)||
       !log(services,"PPRMS","ProgramParams.h",0x8f,"IBU = %d",parameters.ibu))return false;
    double first=0,second=0;
    if(static_cast<std::int32_t>(parameters.water.unit)<8){
        if(!quantity_in(parameters.water,Unit::Liter,first,&services)||!quantity_in(parameters.water,Unit::Gallon,second,&services)||
           !log(services,"PPRMS","ProgramParams.h",0x91,"Water = %3.2f L (%3.2f Gal)",first,second))return false;
    }else if(!log(services,"PPRMS","ProgramParams.h",0x93,"Water = <undefined>"))return false;
    if(static_cast<std::int32_t>(parameters.intensity.unit)<8){if(!log(services,"PPRMS","ProgramParams.h",0x96,"Intensity = %3.0f point",parameters.intensity.value))return false;}
    else if(!log(services,"PPRMS","ProgramParams.h",0x98,"Intensity = <undefined>"))return false;
    if(static_cast<std::int32_t>(parameters.temperature.unit)<8){
        if(!quantity_in(parameters.temperature,Unit::Celsius,first,&services)||!quantity_in(parameters.temperature,Unit::Fahrenheit,second,&services)||
           !log(services,"PPRMS","ProgramParams.h",0x9b,"Temperature = %3.2f C (%3.2f F)",first,second))return false;
    }else if(!log(services,"PPRMS","ProgramParams.h",0x9d,"Temperature = <undefined>"))return false;
    return log(services,"PPRMS",nullptr,0,"isResumedSession = %s",parameters.resumed_session?"True":"False");
}
bool report_program_stats(const ConsoleServices& services,const ProgramStats& stats){
    return log(services,"PST","SessionState.h",0x2f,"heatSlope = %3.1f C/min",stats.heating_rate_celsius_per_minute)&&
        log(services,"PST","SessionState.h",0x30,"DurationSec = %d",static_cast<std::int32_t>(stats.duration_seconds))&&
        log(services,"PST","SessionState.h",0x31,"Pressure = %3.1fPa",stats.pressure_pascal)&&
        log(services,"PST","SessionState.h",0x32,"MaxTemp = %3.1fC",stats.maximum_temperature_celsius)&&
        log(services,"PST","SessionState.h",0x33,"MaxTempAddedSec = %d",static_cast<std::int32_t>(stats.maximum_temperature_added_seconds));
}
bool report_program_buffer(const ConsoleServices& services,const ProgramBuffer& buffer){
    return report_program_record(services,buffer.record())&&report_program_parameters(services,buffer.parameters)&&report_program_stats(services,buffer.stats);
}
bool report_program_estimate(const ConsoleServices& services,const ProgramRecord& record,double heating_rate,std::uint32_t seconds){
    char duration[24];duration_text(duration,sizeof duration,seconds);
    return record.view.name&&log(services,"PGM",nullptr,0,"name = %s",record.view.name)&&
        log(services,"PGM","Program.h",0xac,"timeLeft @%3.1fC/min: %s",heating_rate,duration);
}
}} // namespace zseries::oem
