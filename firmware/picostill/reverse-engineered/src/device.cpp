#include "picostill/device.hpp"
namespace picostill { namespace oem {
void ApplicationDevice::configure(const char* ssid,const char* password) {
    app_.data.set_ssid(ssid,diagnostics_);app_.data.set_password(password,diagnostics_);
    bounded_copy(app_.stored.ssid.data(),app_.stored.ssid.size(),app_.ssid.data(),diagnostics_);
    bounded_copy(app_.stored.password.data(),app_.stored.password.size(),app_.password.data(),diagnostics_);
    ports_.credential_variable("MAIN","SSID",app_.ssid.data(),false);
    ports_.credential_variable("MAIN","PASS",app_.password.data(),true);
    app_.credentials_changed=true;
}
void ApplicationDevice::factory_result(FactoryResult& result) {
    diagnostics_.require(controller_.full_result.runtime!=0 && hardware_.recorded_count!=0,"Controller.h",0xb2,"_result.hasTestRun()");
    result.test=controller_.full_result;result.pressure=hardware_.recorded_pressure;result.pressure_count=hardware_.recorded_count;
}
}} // namespace picostill::oem
