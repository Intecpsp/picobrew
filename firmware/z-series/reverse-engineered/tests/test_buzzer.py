"""Original buzzer/alarm behavior, composition and required SDK boundaries."""
import gzip
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import buzzer_cases as inputs
import buzzer_native as native
import run_msp_emulation as common

ROOT = common.COMPONENT


class BuzzerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='buzzer-test-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.native = native.load_native(cls.temp.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/buzzer-original.json.gz').read_bytes()))

    def test_original_buzzer_alarm_timer_and_console_operations(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture['cases']))
        for index, (case, expected) in enumerate(zip(cases, self.fixture['cases'])):
            with self.subTest(index=index, actions=case['actions']):
                self.assertEqual(native.run(self.native, case), expected)

    def test_executed_original_ranges_and_source_identity(self):
        proof = json.loads((ROOT / 'analysis/buzzer-validation.json').read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture['input_sha256'])
        self.assertEqual(common.shared.digest(ROOT / 'tests/fixtures/buzzer-original.json.gz'), proof['compressed_fixture_sha256'])
        self.assertEqual(proof['cases'], len(inputs.cases()))
        self.assertTrue(proof['native_validation']['all_results_match'])
        for group in ('generator_files', 'validated_source_sha256'):
            for path, expected in proof[group].items():
                with self.subTest(path=path):
                    self.assertEqual(common.shared.digest(ROOT / path), expected)
        target = json.loads((ROOT / 'analysis/buzzer-target-build.json').read_text())
        self.assertFalse(target['complete_application_linked'])
        self.assertFalse(target['hardware_tested'])
        for entry in target['buzzer_sources']:
            self.assertEqual(common.shared.digest(ROOT / entry['source']), entry['source_sha256'])
        for path, expected in target['buzzer_headers_sha256'].items():
            self.assertEqual(common.shared.digest(ROOT / path), expected)
        raw = common.INPUT.read_bytes()
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s['load_address'], offset=s['file_offset'], size=s['size_bytes']) for s in parsed['segments']]
        for function in self.fixture['original_functions']:
            for span in function['ranges']:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span['address'], span['size'])).hexdigest(), span['sha256'])

    def test_head_alarm_and_pwm_adapter_composition(self):
        # The test adapter records actual LEDC method selection and arguments;
        # the target build separately compiles against the real historical SDK.
        directory = Path(self.temp.name)
        header = directory / 'Arduino.h'
        source, binary = directory / 'integration.cpp', directory / 'integration'
        header.write_text('''#pragma once
#include <cstdint>
std::uint32_t millis();
void ledcAttachPin(std::uint8_t,std::uint8_t);
double ledcSetup(std::uint8_t,double,std::uint8_t);
void ledcWrite(std::uint8_t,std::uint32_t);
''')
        source.write_text(r'''
#include "zseries/buzzer.hpp"
#include "zseries/head_flow.hpp"
#include "buzzer_platform.hpp"
#include <cassert>
#include <cstring>
#include <vector>
using namespace zseries::oem;
struct Fixture {
    Buzzer buzzer;SystemAlarmState alarm;HeadFlow head;
    unsigned begins=0, diagnostics=0, assertions=0;std::vector<BuzzerCommand> commands;
    std::vector<unsigned> writes, actions;std::uint32_t clock=1000;
    bool fail_write=false,fail_diagnostic=false,fail_register=false,fail_assertion=false;
    esp32::BuzzerBindings bindings;
};
static Fixture* fixture=nullptr;
std::uint32_t millis(){return fixture->clock++;}
void ledcAttachPin(std::uint8_t pin,std::uint8_t channel){assert(pin==13&&channel==1);fixture->actions.push_back(100);}
double ledcSetup(std::uint8_t channel,double frequency,std::uint8_t resolution){
    assert(channel==1&&frequency==4800&&resolution==8);fixture->actions.push_back(101);return 0;
}
void ledcWrite(std::uint8_t channel,std::uint32_t duty){assert(channel==1);fixture->writes.push_back(duty);fixture->actions.push_back(102);}
static bool begin(void* c,const char* name){assert(!std::strcmp(name,"Buzzer"));++static_cast<Fixture*>(c)->begins;return true;}
static bool add(void* c,Buzzer& buzzer,const BuzzerCommandDefinition& value){
    auto& f=*static_cast<Fixture*>(c);assert(&buzzer==&f.buzzer);f.commands.push_back(value.command);return !f.fail_register;
}
static bool assertion(void* c,std::uint32_t line){
    auto& f=*static_cast<Fixture*>(c);assert(line==0x118);++f.assertions;return !f.fail_assertion;
}
static bool diagnostic(void* c,BuzzerDiagnostic,std::uint32_t,const BuzzerState&,bool){
    auto& f=*static_cast<Fixture*>(c);++f.diagnostics;return !f.fail_diagnostic;
}
static bool head_effect(void* c,const HeadAction& action){
    auto& f=*static_cast<Fixture*>(c);
    if(action.effect==HeadEffect::SystemAlarm)return f.buzzer.set_alarm(f.alarm,action.argument!=0)==BuzzerResult::Ok;
    // This test uses only HeadFlow::alarm, not a composed UI or Core task.
    assert(false);return false;
}
static std::uint32_t head_clock(void*){return millis();}
int main(){
    Fixture f;fixture=&f;f.bindings.context=&f;f.bindings.begin_console=begin;
    f.bindings.add_command=add;f.bindings.assertion=assertion;f.bindings.diagnostic=diagnostic;f.buzzer.services=esp32::buzzer_services(f.bindings);
    f.head.services.context=&f;f.head.services.millis=head_clock;f.head.services.perform=head_effect;
    assert(f.buzzer.setup()==BuzzerResult::Ok&&f.begins==1&&f.commands.size()==5);
    assert((f.actions==std::vector<unsigned>{100,101,102})&&f.writes.back()==0);
    assert(f.head.alarm(true)==HeadFlowResult::Ok&&f.alarm.enabled&&f.buzzer.state.enabled);
    assert(f.buzzer.tick()==BuzzerResult::Ok&&f.writes.back()==125);
    assert(f.buzzer.tick()==BuzzerResult::Ok&&f.writes.back()==0);
    assert(f.buzzer.tick()==BuzzerResult::Ok&&f.writes.back()==125);
    const auto count=f.writes.size();assert(f.head.alarm(false)==HeadFlowResult::Ok);
    assert(!f.alarm.enabled&&!f.buzzer.state.enabled&&f.writes.size()==count);
    assert(f.buzzer.tick()==BuzzerResult::Ok&&f.writes.size()==count+1&&f.writes.back()==0);
    assert(f.buzzer.tick()==BuzzerResult::Ok&&f.writes.size()==count+1);
    // Information and setf/setd are concrete command dispatches. The numeric
    // operations update state only; they do not immediately reconfigure PWM.
    assert(f.buzzer.command(BuzzerCommand::Information)==BuzzerResult::Ok);
    const auto actions=f.actions.size();
    assert(f.buzzer.command(BuzzerCommand::Frequency,"-1")==BuzzerResult::Ok&&f.buzzer.state.frequency_hz==0xffffffffU);
    assert(f.buzzer.command(BuzzerCommand::Duty,"255 trailing")==BuzzerResult::Ok&&f.buzzer.state.duty==255);
    assert(f.actions.size()==actions);
    // Missing dependencies and failed callbacks never become success.
    Buzzer empty;assert(empty.setup()==BuzzerResult::MissingService);
    assert(empty.tick()==BuzzerResult::MissingService);
    assert(empty.command(BuzzerCommand::Information)==BuzzerResult::MissingService);
    assert(empty.command(BuzzerCommand::Duty,nullptr)==BuzzerResult::MissingService);
    SystemAlarmState alarm;assert(empty.set_alarm(alarm,true)==BuzzerResult::MissingService&&!alarm.enabled);
    empty.state.enabled=true;assert(empty.set_alarm(alarm,false)==BuzzerResult::MissingService&&empty.assertion_line==0x118);
    // A mismatched flag invokes the returning assertion service, then carries
    // out the original requested transition instead of terminating early.
    f.alarm.enabled=true;f.buzzer.state.enabled=false;
    assert(f.buzzer.set_alarm(f.alarm,false)==BuzzerResult::Ok&&f.assertions==1);
    assert(!f.alarm.enabled&&!f.buzzer.state.enabled&&f.buzzer.assertion_line==0x118);
    f.alarm.enabled=false;f.buzzer.state.enabled=true;
    const auto configure=f.buzzer.services.configure;f.buzzer.services.configure=nullptr;
    assert(f.buzzer.set_alarm(f.alarm,true)==BuzzerResult::Ok&&f.assertions==2);
    f.buzzer.services.configure=configure;
    assert(f.alarm.enabled&&f.buzzer.state.enabled);
    // Unchanged alarm still asserts, and leaves a mismatched buzzer unchanged.
    f.alarm.enabled=false;
    assert(f.buzzer.set_alarm(f.alarm,false)==BuzzerResult::Ok&&f.assertions==3&&f.buzzer.state.enabled);
    f.fail_assertion=true;
    assert(f.buzzer.set_alarm(f.alarm,true)==BuzzerResult::ServiceFailure&&!f.alarm.enabled);
    f.fail_assertion=false;f.buzzer.state.enabled=false;
    f.buzzer.state.frequency_hz=4800;f.bindings.add_command=nullptr;
    assert(f.buzzer.register_commands()==BuzzerResult::ServiceFailure);
    f.bindings.add_command=add;f.fail_register=true;
    assert(f.buzzer.register_commands()==BuzzerResult::ServiceFailure);
    f.fail_diagnostic=true;
    assert(f.buzzer.set_alarm(f.alarm,true)==BuzzerResult::ServiceFailure&&!f.alarm.enabled);
    f.bindings.diagnostic=nullptr;
    assert(f.buzzer.command(BuzzerCommand::Information)==BuzzerResult::ServiceFailure);
}
''')
        command = ['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                   '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                   '-I' + str(directory), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'platform/esp32'),
                   str(ROOT / 'src/buzzer.cpp'), str(ROOT / 'src/head_flow.cpp'),
                   str(ROOT / 'platform/esp32/buzzer_platform.cpp'), str(source), '-o', str(binary)]
        subprocess.run(command, check=True)
        environment = dict(os.environ)
        environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        subprocess.run([str(binary)], check=True, env=environment)


if __name__ == '__main__':
    unittest.main()
