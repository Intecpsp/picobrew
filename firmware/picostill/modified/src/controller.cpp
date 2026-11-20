// Modified from the recovered PicoStill 0.0.35 source.
// Foreground deadline scheduling/comparisons are repaired for uint32 millis
// rollover; the original reconstruction remains unchanged in reverse-engineered/.
#include "picostill/controller.hpp"

namespace picostill { namespace oem {
bool Hardware::enabled() const noexcept {
    return (fans.power.enabled && fans.first.enabled) || fans.second_enabled() || pump.enabled || sensors_enabled;
}
bool Hardware::sensors_in_range() { return sensors.metrics.values_in_range(diagnostics_); }
bool Hardware::fans_issue() {
    std::array<FanStatus,2> states;
    states[0].enabled=fans.first.enabled;states[0].speed=fans.first.speed;states[0].metrics=fans.first.metrics;
    states[1].enabled=fans.second.enabled;states[1].speed=fans.second.speed;states[1].metrics=fans.second.metrics;
    return fans_have_issue(states,diagnostics_);
}
bool Hardware::safe_to_resume() const noexcept {
    return safe_to_resume_pump(sensors.values[1],sensors.values[4],maximum_temperature,hysteresis,minimum_pressure);
}
void Hardware::enable_sensors_internal(bool value) {
    if (sensors_enabled==value) {diagnostics_.message("Redundant call to _enable");return;}
    sensors_enabled=value;
    if (!value) record_pressure=false;
    else {next_sample=ports_.millis();sensors.reset_metrics();recorded_pressure.fill(0);recorded_count=0;}
    ports_.variable("SENSORS","IsRunning",sensors_enabled?"True":"False");
    ports_.variable("SENSORS","isRecordingPressure",record_pressure?"True":"False");
}
void Hardware::enable_sensors(bool value) {
    if(sensors_enabled==value) ports_.variable("SENSORS","Redundant call to enable()",sensors_enabled?"true":"false");
    else enable_sensors_internal(value);
}
void Hardware::enable_pressure_recording() {
    if(sensors_enabled) {ports_.variable("SENSORS","Redundant call to enableWithPressureRecording()","true");return;}
    record_pressure=true;enable_sensors_internal(true);
}
void Hardware::capture_into(HardwareSnapshot& target) {
    target.captured_at=ports_.millis();target.sensor_samples=sensors.metrics.temperature_samples;
    target.fan1_samples=fans.first.metrics.samples;target.fan2_samples=fans.second.metrics.samples;
    target.values=sensors.values;target.averages=sensors.metrics.averages;target.success_rates=sensors.metrics.success_rates;
    // Original snapshot writes one duty byte, retaining the three padding bytes.
    target.fan1_speed=(target.fan1_speed&0xffffff00u)|fans.first.speed;
    target.fan2_speed=(target.fan2_speed&0xffffff00u)|fans.second.speed;
    target.fan1_rpm=fans.first.metrics.rpm_average;target.fan2_rpm=fans.second.metrics.rpm_average;
}
HardwareSnapshot Hardware::capture() {HardwareSnapshot result;capture_into(result);return result;}
void Hardware::check_error() {
    if(!fans_issue()) {
        if(!sensors_enabled) return;
        if(sensors_in_range() && (!sensors_ready() || sensors.metrics.read_success_rates_good(diagnostics_))) return;
    }
    if(fans_issue()) error=StillError::FanFailure;
    else if(!sensors_in_range()) error=StillError::SensorOutOfRange;
    else if(sensors_ready() && !sensors.metrics.read_success_rates_good(diagnostics_)) error=StillError::SensorFailure;
    else {diagnostics_.message("Unhandled Hardware Error");error=StillError::UnhandledError;}
    capture_into(error_snapshot);
}
void Hardware::poll() {
    if(fans.first.enabled) fans.first.sample();
    if(fans.second.enabled) fans.second.sample();
    if(sensors_enabled) {
        const auto now=ports_.millis();
        if(static_cast<std::int32_t>(now-next_sample)>=0) {
            sensors.sample_pressure();sensors.sample_temperatures();next_sample=now+sample_interval;
            events_.sensors_sampled();
            if(record_pressure) {
                const float value=sensors.pressure.valid?static_cast<float>(sensors.pressure.value):failed_pressure;
                if(recorded_count<recorded_pressure.size()) recorded_pressure[recorded_count++]=value;
                else diagnostics_.message("count >= SIZE");
            }
        }
    }
    check_error();
}

bool sensor_connections_match(SnapshotPorts& snapshots) {
    const auto sample=snapshots.capture();const auto ambient=sample.values[3];
    for(unsigned i=0;i<3;++i) {
        float difference=sample.values[i]-ambient;
        if(difference<0.0f) difference=ambient-sample.values[i];
        if(!(difference<=10.0f)) return false;
    }
    return true;
}
void ConnectionTest::reset() noexcept {started_at=0;started=false;done=false;error=StillError::None;result=HardwareSnapshot{};}
void ConnectionTest::finish(Hardware& hardware) {
    if(!hardware.sensors_in_range() || hardware.fans_issue()) {
        ports_.variable("HW_TESTS","hw.sensorsAreInRange()",hardware.sensors_in_range()?"True":"False");
        ports_.variable("HW_TESTS","hw.fansHasIssue()",hardware.fans_issue()?"True":"False");
        error=StillError::HardwareNotConnected;
    }
    done=true;hardware.capture_into(result);ports_.variable("ConnectionTest","IsDone","True");
}
void ConnectionTest::tick(std::uint64_t runtime,Hardware& hardware) {
    if(done) return;
    if(!started) {started=true;ports_.variable("CONNECTION_TEST","IS_STARTED","True");started_at=runtime;}
    if(hardware.sensors_ready() && hardware.fans_ready()) finish(hardware);
    // Preserve the independent timeout check; a ready timed-out test finishes twice.
    if(runtime-started_at>=5000) {diagnostics_.message("[ConnectionTest Timeout] (runtime - TestStartDate) >= TEST_TIMEOUT");finish(hardware);}
}
void PinTest::reset() noexcept {started_at=0;started=false;done=false;error=StillError::None;result=HardwareSnapshot{};}
void PinTest::tick(std::uint64_t runtime,SnapshotPorts& snapshots) {
    if(done) return;
    if(!started) {started=true;ports_.variable("PIN_TEST","IS_STARTED","True");started_at=runtime;}
    if(!sensors_passed) {
        sensors_passed=sensor_connections_match(snapshots);
        if(sensors_passed) events_.console_message("Sensors Passed");
        if(!sensors_passed && runtime-started_at<60000) return;
    }
    done=true;snapshots.capture_into(result);
    if(!sensor_connections_match(snapshots)) error=StillError::SensorConnectionWrong;
    ports_.variable("PinTest","IsDone","True");
}

void Controller::reset_pump_test() noexcept {
    pump_test.state=PumpTestState::Starting;pump_test.vacuum_duration=0;
    pump_test.error=StillError::None;pump_test.result=HardwareSnapshot{};
}
void Controller::set_state(HWState next) {
    diagnostics_.require(static_cast<std::uint32_t>(next)<9,"Controller.h",0x26,"isEnumValid(state)");
    const auto now=ports_.millis();
    if(state==next) {diagnostics_.message("Redundant call to setHardwareState");return;}
    switch(next) {
    case HWState::Off:hardware_.fans.set_power(false);hardware_.enable_sensors(false);hardware_.pump.set_pump(false);break;
    case HWState::On:hardware_.fans.set_power(true);hardware_.enable_sensors(true);hardware_.pump.set_pump(true);overtemperature_deadline=0;break;
    case HWState::CondenserOutputOverTemp:overtemperature_deadline=now+overtemperature_holdoff;break;
    case HWState::PumpOff:hardware_.pump.set_pump(false);break;
    case HWState::Cleaning:hardware_.pump.set_pump(true);break;
    case HWState::PumpTest:reset_pump_test();hardware_.sensors.sample_pressure();hardware_.enable_sensors(true);hardware_.pump.set_pump(true);break;
    case HWState::HardwareTest:connection_test.reset();hardware_.enable_sensors(true);hardware_.fans.set_power(true);break;
    case HWState::PinTest:pin_test.reset();hardware_.enable_sensors(true);hardware_.fans.set_power(true);break;
    case HWState::FullTest:reset_pump_test();connection_test.reset();pin_test.reset();hardware_.enable_pressure_recording();
        hardware_.sensors.sample_pressure();hardware_.pump.set_pump(true);hardware_.fans.set_power(true);break;
    default:if(static_cast<std::uint32_t>(next)==9)diagnostics_.message("Invalid state = HWState::_EnumCount");break;
    }
    ports_.variable("CTL","HW_STATE",to_string(next));state=next;state_changed_at=now;
}
void Controller::start(ProgramId requested) {
    diagnostics_.require(!running(),"Controller.h",0x132,"!getIsRunning()");
    diagnostics_.require(requested!=ProgramId::None,"Controller.h",0x133,"programParams.Id != ProgramId::None");
    if(running() || requested==ProgramId::None) return;
    switch(requested) {
    case ProgramId::PicoEssentialOils:case ProgramId::PicoManual:set_state(HWState::On);break;
    case ProgramId::Cleaning:case ProgramId::RequestedCleaning:set_state(HWState::Cleaning);break;
    case ProgramId::PumpTest:set_state(HWState::PumpTest);break;
    case ProgramId::HardwareTest:set_state(HWState::HardwareTest);break;
    case ProgramId::PinTest:set_state(HWState::PinTest);break;
    case ProgramId::FullTest:set_state(HWState::FullTest);break;
    default:break;
    }
    ports_.variable("CTL","START",to_string(requested));program=requested;program_started_at=ports_.millis();error=StillError::None;
    events_.controller_changed();
}
void Controller::stop() {
    diagnostics_.require(running(),"Controller.h",0x161,"getIsRunning()");if(!running())return;
    if(program==ProgramId::PumpTest) {if(pump_test.error!=StillError::None)error=pump_test.error;events_.snapshot_report(pump_test.result);}
    else if(program==ProgramId::HardwareTest) {if(connection_test.error!=StillError::None)error=connection_test.error;events_.snapshot_report(connection_test.result);}
    else if(program==ProgramId::PinTest) {if(pin_test.error!=StillError::None)error=pin_test.error;events_.snapshot_report(pin_test.result);}
    else if(program==ProgramId::FullTest) {
        ports_.variable("CTL","TEST_RESULT_PUMP",to_string(pump_test.error));ports_.variable("CTL","TEST_RESULT_HW",to_string(connection_test.error));
        ports_.variable("CTL","TEST_RESULT_PINS",to_string(pin_test.error));
        const auto secondary=connection_test.error!=StillError::None?connection_test.error:pin_test.error;
        if(secondary==StillError::None) {if(pump_test.error!=StillError::None)error=pump_test.error;}
        else if(pump_test.error==StillError::None) {
            // Original pin-only failure leaves the previous error unchanged.
            if(connection_test.error!=StillError::None)error=connection_test.error;
        } else {
            const auto pump=static_cast<unsigned>(pump_test.error);
            if(pump>=9 && pump<=11)error=static_cast<StillError>(pump+(secondary==StillError::HardwareNotConnected?6:9));
            else diagnostics_.message("Invalid pump error");
        }
        full_result.error=error;full_result.runtime=static_cast<std::uint32_t>(ports_.millis()-program_started_at);
        const auto& result=connection_test.error==StillError::None?pump_test.result:connection_test.result;
        full_result.values=result.values;full_result.success_rates=result.success_rates;events_.full_test_report(full_result);
    }
    ports_.variable("CTL","STOP",to_string(program));ports_.variable("CTL","ERROR",to_string(error));set_state(HWState::Off);events_.controller_changed();
}
void Controller::poll() {
    hardware_.poll();if(!running())return;
    const auto now=ports_.millis();const std::uint32_t runtime=now-program_started_at;
    const auto numeric=static_cast<std::uint32_t>(state);
    if(numeric-5u>3u) {
        if(hardware_.error!=StillError::None) {error=hardware_.error;stop();}
        if(hardware_.pump.enabled && !hardware_.fans.second_enabled()) {
            ports_.variable("CTL","SAFETY","Pump ON t.f. Fan2 ON");hardware_.fans.power.set(true);hardware_.fans.second.set(true,255);
        }
    }
    switch(state) {
    case HWState::On:if(hardware_.sensors.values[1]>hardware_.maximum_temperature)set_state(HWState::CondenserOutputOverTemp);break;
    case HWState::CondenserOutputOverTemp:
        if(hardware_.sensors.values[1]<=hardware_.maximum_temperature-hardware_.hysteresis)set_state(HWState::On);
        else if(static_cast<std::int32_t>(now-overtemperature_deadline)>=0)set_state(HWState::PumpOff);
        break;
    case HWState::PumpOff:if(hardware_.safe_to_resume())set_state(HWState::On);break;
    case HWState::Cleaning:if(runtime>=cleaning_duration)stop();break;
    case HWState::PumpTest:pump_test.tick(now,runtime);if(pump_test.state==PumpTestState::Done)stop();break;
    case HWState::HardwareTest:connection_test.tick(runtime,hardware_);if(connection_test.done)stop();break;
    case HWState::PinTest:pin_test.tick(runtime,hardware_);if(pin_test.done)stop();break;
    case HWState::FullTest:
        pump_test.tick(now,runtime);
        if(!pin_test.done)pin_test.tick(runtime,hardware_);
        else {
            if(!connection_test.started) {ports_.variable("CTL","FULL_TEST","pinTest -> connectionTest");hardware_.fans.set_power(true);}
            connection_test.tick(runtime,hardware_);
        }
        if(pump_test.state==PumpTestState::Done && connection_test.done && pin_test.done)stop();
        break;
    default:if(numeric==9)diagnostics_.message("Invalid _hardwareState = HWState::_EnumCount");break;
    }
}
}} // namespace picostill::oem
