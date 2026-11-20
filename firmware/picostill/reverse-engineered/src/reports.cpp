#include "picostill/reports.hpp"
#include <cstring>
namespace picostill { namespace oem {
void ApplicationReports::append(const char* value,std::size_t size) {
    if(!value || !size)return;
    diagnostics_.check(size<remaining,"datalen < _left Buffer not big enough");
    const std::size_t copied=size<remaining?size:remaining;
    std::memcpy(csv.data()+length,value,copied);length+=copied;remaining-=copied;
}
void ApplicationReports::controller_changed() {
    if(!hardware_ || !controller_){diagnostics_.message("Controller report called before binding");return;}
    csv.fill(0);length=0;remaining=127;
    for(unsigned i=0;i<5;++i){
        const double value=i<4?static_cast<double>(hardware_->sensors.values[i])*1.8+32.0:static_cast<double>(hardware_->sensors.values[i]);
        // The original local is 16 bytes. The owned bounded formatting port
        // prevents a corrupted/extreme sensor value from overwriting its stack.
        char number[96]={0};ports_.format_decimal(value,4,2,number,sizeof(number));append(number,std::strlen(number));append(",",1);
    }
    append(hardware_->pump.enabled?"1":"0",1);append(",",1);
    append(hardware_->fans.second_enabled()?"1":"0",1);append(",",1);
    append(controller_->state==HWState::Off?"0":"1",1);append(",",1);
    const char* error=to_string(controller_->error);append(error,std::strlen(error));hardware_ports_.variable("CTL","STATE",csv.data());
}
void ApplicationReports::snapshot_report(const HardwareSnapshot& value) {
    ports_.integer("HW_SNAP","Time",static_cast<std::uint32_t>(value.captured_at));ports_.integer("HW_SNAP","SensorSamples",value.sensor_samples);
    const char* names[]={"SensorValues.In","SensorValues.Out","SensorValues.Pot","SensorValues.Ambient","SensorValues.Pressure"};
    const char* averages[]={"InTempAverage","OutTempAverage","PotTempAverage","AmbientTempAverage","PressureAverage"};
    const char* rates[]={"SensorReadSuccessRates.In","SensorReadSuccessRates.Out","SensorReadSuccessRates.Pot","SensorReadSuccessRates.Ambient","SensorReadSuccessRates.Pressure"};
    for(unsigned i=0;i<5;++i){ports_.floating("HW_SNAP",names[i],value.values[i]);ports_.floating("HW_SNAP",averages[i],value.averages[i]);ports_.floating("HW_SNAP",rates[i],value.success_rates[i]);}
    ports_.integer("HW_SNAP","Fan1Samples",value.fan1_samples);ports_.integer("HW_SNAP","Fan1SetSpeed",static_cast<std::uint8_t>(value.fan1_speed));ports_.floating("HW_SNAP","Fan1rpm",value.fan1_rpm);
    ports_.integer("HW_SNAP","Fan2Samples",value.fan2_samples);ports_.integer("HW_SNAP","Fan2SetSpeed",static_cast<std::uint8_t>(value.fan2_speed));ports_.floating("HW_SNAP","Fan2rpm",value.fan2_rpm);
}
void ApplicationReports::full_test_report(const FullTestResult& value) {
    hardware_ports_.variable("TEST_RESULT","Error",to_string(value.error));ports_.integer("TEST_RESULT","Duration",static_cast<std::uint32_t>(value.runtime));
    const char* names[]={"In","Out","Pot","Ambient","Pressure"};
    hardware_ports_.variable("TEST_RESULT","DATA","READ RATES");for(unsigned i=0;i<5;++i)ports_.floating("TEST_RESULT",names[i],value.success_rates[i]);
    hardware_ports_.variable("TEST_RESULT","DATA","VALUES");for(unsigned i=0;i<5;++i)ports_.floating("TEST_RESULT",names[i],value.values[i]);
}
}} // namespace picostill::oem
