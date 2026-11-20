#define ARDUINOJSON_USE_DOUBLE 1
#define ARDUINOJSON_USE_LONG_LONG 0
#include <ArduinoJson.h>
#include "zseries/serial_logger.hpp"
#include <iostream>
#include <string>
#include <cstring>
using namespace zseries::oem;
namespace {
struct Context {ArduinoJson::JsonVariantConst input;ArduinoJson::JsonArray effects,assertions;std::uint32_t now=1000,counter=7;};
bool check(void* p,const char* file,std::uint32_t line,const char* expression){auto& c=*static_cast<Context*>(p);auto row=c.assertions.createNestedArray();row.add(file);row.add(line);row.add(expression);++c.counter;c.now+=200;auto e=c.effects.createNestedArray();e.add("delay");e.add(200);return true;}
bool emit(void* p,SerialLogMessage kind,const char* text,std::uint32_t value){auto& c=*static_cast<Context*>(p);auto row=c.effects.createNestedArray();row.add(static_cast<unsigned>(kind));row.add(text?std::string(text):std::string());row.add(value);return true;}
bool console_log(void*,ConsoleLevel,const char*,const char*,std::uint32_t,const char*){return true;}
SerialLogResult fill(void* p,CsvBuffer& b,const SerialLogServices& s){auto& c=*static_cast<Context*>(p);const char* value=c.input["fill"].as<const char*>();if(!value)value="fixture";return (c.input["formatted"]|false)?b.format(s,"%s",value):b.append(value,s);}
}
int main(){std::string line;while(std::getline(std::cin,line)){
    ArduinoJson::DynamicJsonDocument input(16384),output(32768);if(ArduinoJson::deserializeJson(input,line)!=ArduinoJson::DeserializationError::Ok)return 2;
    auto in=input.as<ArduinoJson::JsonVariantConst>();output["id"]=in["id"].as<const char*>();Context c;c.input=in;c.effects=output.createNestedArray("effects");c.assertions=output.createNestedArray("assertions");c.now=in["clock"]|1000u;c.counter=in["counter"]|7u;
    SerialLogger logger;logger.services.context=&c;logger.services.assertion=check;logger.services.emit=emit;logger.enabled=in["enabled"]|false;logger.deadline=in["deadline"]|0u;logger.interval_ms=in["interval"]|1000u;logger.minimum_interval_ms=in["minimum"]|200u;logger.ever_truncated=in["ever"]|false;
    HeaterState heater;HeaterSensors temp;AuxiliarySensors auxiliary;z_msp_state msp{};ProgramRuntimeState runtime;PicoStillSamples still;CoreCsvSources sources;
    double d[22];std::uint32_t w[16];for(unsigned i=0;i<22;++i)d[i]=in["d"][i].as<double>();for(unsigned i=0;i<16;++i)w[i]=in["w"][i].as<std::uint32_t>();
    heater.target_celsius=d[0];heater.output_ticks=d[1];heater.integral_display=d[2];heater.started=w[0];temp.previous_wort=d[3];temp.wort_rate=d[4];temp.previous_hex=d[5];temp.hex_rate=d[6];temp.temperatures[1].celsius=d[7];temp.temperatures[0].celsius=d[8];temp.temperatures[2].celsius=d[9];temp.temperatures[3].celsius=d[10];auxiliary.pressure_pa=d[11];sources.reference_pressure_pa=static_cast<float>(d[12]);temp.estimated_boil_celsius=d[13];for(unsigned i=0;i<4;++i)still.current.temperature[i]=d[14+i];still.current.pressure=d[18];still.pressure.squared_deviations=d[19];still.t1.squared_deviations=d[20];still.t2.squared_deviations=d[21];still.pressure.count=w[13];still.t1.count=w[14];still.t2.count=w[15];
    msp.errors=w[1];msp.fan=w[2];msp.pumps=w[3];msp.valve_position=w[4];msp.moving_valve=w[5];msp.status_byte_2=w[6];msp.status_byte_3=w[7];runtime.total_left_seconds=w[8];runtime.instruction_left_seconds=w[9];runtime.instruction.opcode=static_cast<OpCode>(w[10]);runtime.instruction.argument=static_cast<std::int32_t>(w[11]);runtime.step_count=w[12];runtime.step_name=in["step"].as<const char*>();if(!runtime.step_name)runtime.step_name="Mash";
    sources.heater=&heater;sources.temperatures=&temp;sources.auxiliary=&auxiliary;sources.msp=&msp;sources.runtime=&runtime;sources.still=&still;logger.source=core_csv_source(sources);
    if(in["fixture"]|false){logger.source.context=&c;logger.source.header=fill;logger.source.data=fill;}
    const char* prefix=in["prefix"].as<const char*>();if(prefix){logger.buffer.length=std::strlen(prefix);std::memcpy(logger.buffer.text.data(),prefix,logger.buffer.length+1);}logger.buffer.frozen=in["frozen"]|false;logger.buffer.truncated=in["truncated"]|false;
    const std::string op=in["op"].as<const char*>();SerialLogResult result=SerialLogResult::Ok;
    if(op=="header")result=logger.header();else if(op=="data")result=logger.regenerate();else if(op=="out")result=logger.out();else if(op=="enable")result=logger.enable(in["value"]|false);else if(op=="tick")result=logger.tick(in["now"]|0u);else if(op=="info")result=logger.information();else if(op=="interval")result=logger.set_interval(in["argument"].as<const char*>());
    else if(op=="append")result=logger.buffer.append(in["argument"].as<const char*>(),logger.services);
    else if(op=="format")result=logger.buffer.format(logger.services,(in["null_format"]|false)?nullptr:"%s",in["argument"].as<const char*>());
    else if(op=="console"){ConsoleEntry entries[6];ConsoleServices services;services.context=&c;services.assertion=check;services.log=console_log;ConsoleShell shell(SerialLoggerConsoleName,entries,6,services);register_serial_logger_console(shell,logger);output["name"]=shell.name();auto rows=output.createNestedArray("entries");for(std::size_t i=0;i<shell.size();++i){auto r=rows.createNestedArray();r.add(shell.entry(i)->exact_match?1:0);r.add(shell.entry(i)->token);r.add(shell.entry(i)->description);}}
    else return 3;
    if(result!=SerialLogResult::Ok)return 4;
    output["text"]=std::string(logger.buffer.text.data());output["length"]=logger.buffer.length;output["truncated"]=logger.buffer.truncated?1:0;output["frozen"]=logger.buffer.frozen?1:0;output["enabled"]=logger.enabled?1:0;output["ever"]=logger.ever_truncated?1:0;output["deadline"]=logger.deadline;output["interval"]=logger.interval_ms;output["clock"]=c.now;output["counter"]=c.counter;
    ArduinoJson::serializeJson(output,std::cout);std::cout<<'\n';
}}
