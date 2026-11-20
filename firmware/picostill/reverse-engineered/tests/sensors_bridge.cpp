#include "picostill/sensors.hpp"
#include <cstring>
#include <string>
#include <vector>
using namespace picostill::oem;
namespace {
float number(std::uint32_t raw){float value;std::memcpy(&value,&raw,4);return value;}
std::uint32_t bits(float value){std::uint32_t raw;std::memcpy(&raw,&value,4);return raw;}
struct Ports:DiagnosticPorts,I2cPorts {
    std::vector<std::string> events;
    const int* rx=nullptr;
    unsigned count=0,index=0,received=0;
    int ready=0;
    void assertion(const char* file,unsigned line,const char* text) override {events.push_back(std::string("assertion:")+file+":"+std::to_string(line)+":"+text);}
    void message(const char* text) override {events.push_back(std::string("message:")+text);}
    void delay_ms(std::uint32_t value) override {events.push_back("delay:"+std::to_string(value));}
    void begin_transmission(std::uint8_t address) override {events.push_back("begin:"+std::to_string(address));}
    void write_byte(std::uint8_t value) override {events.push_back("write:"+std::to_string(value));}
    void end_transmission() override {events.push_back("end");}
    unsigned request(std::uint8_t address,unsigned size) override {events.push_back("request:"+std::to_string(address)+":"+std::to_string(size));return received;}
    int available() override {events.push_back("available:"+std::to_string(ready));return ready;}
    int read_byte() override {const int value=index<count?rx[index++]:-1;events.push_back("read:"+std::to_string(value));return value;}
} ports;
Diagnostics diagnostics(ports);
std::vector<std::uint32_t> result;
void reset(){ports.events.clear();ports.index=0;diagnostics.assertion_count=0;result.clear();}
}
extern "C" {
double sensor_convert(unsigned channel,std::int32_t adc){reset();const auto* p=board_thermistor(channel);return p?thermistor_kelvin(*p,adc,diagnostics):0;}
int sensor_parameters(unsigned channel,std::uint32_t* out){
    const auto* p=board_thermistor(channel);if(!p)return 0;
    const double values[]={p->a,p->b,p->c,p->series_resistance_ohms};std::memcpy(out,values,sizeof(values));out[8]=bits(p->adc_max);return 1;
}
void sensor_run(unsigned op,const std::uint32_t* w,const int* rx,unsigned count){
    reset();ports.rx=rx;ports.count=count;ports.received=w[3];ports.ready=static_cast<int>(w[4]);
    Sensors sensors(ports,diagnostics);sensors.adc.three_channels=w[0]!=0;sensors.pressure.expected_status=w[1];sensors.pressure.range=w[2];
    sensors.metrics.temperature_samples=w[5];sensors.metrics.pressure_samples=w[6];
    for(unsigned i=0;i<5;i++){sensors.values[i]=number(w[7+i]);sensors.metrics.success_rates[i]=number(w[12+i]);sensors.metrics.averages[i]=number(w[17+i]);}
    if(op==0)sensors.adc.read(ports);else if(op==1)sensors.pressure.read(ports);
    else if(op==2)sensors.sample_temperatures();else sensors.sample_pressure();
    for(auto value:sensors.adc.raw)result.push_back(value);
    for(bool valid:sensors.adc.valid)result.push_back(valid);
    std::uint64_t pressure_bits;std::memcpy(&pressure_bits,&sensors.pressure.value,8);
    result.push_back(static_cast<std::uint32_t>(pressure_bits));result.push_back(static_cast<std::uint32_t>(pressure_bits>>32));result.push_back(sensors.pressure.valid);
    for(float value:sensors.values)result.push_back(bits(value));
    result.push_back(sensors.metrics.temperature_samples);result.push_back(sensors.metrics.pressure_samples);
    for(float value:sensors.metrics.success_rates)result.push_back(bits(value));
    for(float value:sensors.metrics.averages)result.push_back(bits(value));
}
unsigned sensor_result_count(){return static_cast<unsigned>(result.size());}
std::uint32_t sensor_result(unsigned i){return result.at(i);}
unsigned sensor_event_count(){return static_cast<unsigned>(ports.events.size());}
const char* sensor_event(unsigned i){return ports.events.at(i).c_str();}
std::uint32_t sensor_assertions(){return diagnostics.assertion_count;}
}
