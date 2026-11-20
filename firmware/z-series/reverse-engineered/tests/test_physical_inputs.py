import gzip
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_physical_inputs as validation

class PhysicalInputTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-physical-input-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / 'tests/fixtures/physical-inputs-original.json.gz').read_bytes()))

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def test_original_button_encoder_gpio_event_and_ring_sequences(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.native_case(self.native, row['input']), row['original'])

    def test_source_original_and_target_evidence_identity(self):
        report = json.loads((ROOT / 'analysis/physical-inputs-validation.json').read_text())
        self.assertTrue(report['all_results_match']); self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'], len(validation.input_cases()))
        self.assertEqual(report['golden_cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/physical-inputs-original.json.gz'))
        self.assertEqual(report['input_sha256'], validation.digest(ROOT.parent / 'oem/upstream/zseries_0_0_119.bin'))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path): self.assertEqual(validation.digest(ROOT / path), expected)
        target = json.loads((ROOT / 'analysis/physical-inputs-target-validation.json').read_text())
        self.assertTrue(target['isr_sections_verified']); self.assertFalse(target['hardware_tested'])
        for row in target['owned_sources']:
            self.assertEqual(validation.digest(ROOT / row['source']), row['source_sha256'])
        for path, expected in target['owned_header_sha256'].items():
            self.assertEqual(validation.digest(ROOT / path), expected)
        self.assertEqual(target['checker_sha256'], validation.digest(ROOT / 'tools/verify_physical_input_target.py'))
        self.assertEqual(target['sdk_gpio_read']['calls'], [])
        self.assertTrue(all(row['section'].startswith('.iram') for row in target['isr_symbols']))

    def test_head_core_composition_and_service_failure_guards(self):
        source = Path(self.build.name) / 'input_integration.cpp'; binary = Path(self.build.name) / 'input_integration'
        source.write_text(r'''
#include "zseries/physical_inputs.hpp"
#include "zseries/head_encoder.hpp"
#include "zseries/head_events.hpp"
#include <array>
#include <cassert>
using namespace zseries::oem;
struct Fixture {
    PhysicalEncoder input; HeadEncoder head; HeadEvents buttons;
    EepromData settings; CoreFlow core; PhysicalButton power;
    std::array<int, 256> levels{}; std::uint32_t now=0;
    unsigned clicks=0, forwarded=0, draws=0, registrations=0, interrupts=0;
    Fixture():core(settings),power(core.button_events){levels.fill(1);}
};
static std::uint32_t clock(void* p){return static_cast<Fixture*>(p)->now;}
static bool mode(void*,std::uint8_t,std::uint8_t){return true;}
static bool read(void* p,std::uint8_t pin,std::int32_t& value){value=static_cast<Fixture*>(p)->levels[pin];return true;}
static bool attach(void* p,std::uint8_t,InputInterrupt,std::uint8_t){++static_cast<Fixture*>(p)->interrupts;return true;}
static bool registration(void* p,PhysicalEncoder&){++static_cast<Fixture*>(p)->registrations;return true;}
static bool head_effect(void* p,HeadEncoderEffect,std::uint32_t,std::uint32_t){++static_cast<Fixture*>(p)->draws;return true;}
static bool button_effect(void* p,HeadEventEffect effect,std::uint32_t){
    auto& f=*static_cast<Fixture*>(p);assert(effect==HeadEventEffect::ScreenButton);++f.clicks;return true;
}
static bool forward_button(void* p,const CoreButtonEvent& event){
    auto& f=*static_cast<Fixture*>(p);assert(event.id==1);++f.forwarded;
    return f.buttons.button_event(event.type!=0)==HeadEventResult::Ok;
}
static bool forward_rotation(void* p,std::int32_t delta){return static_cast<Fixture*>(p)->head.rotate(delta)==HeadEncoderResult::Ok;}
static bool core_tick(void*,CoreTickStage,std::uint32_t){return true;}
static bool core_effect(void*,const CoreAction&,std::uint32_t&){return false;}
static void tick(Fixture& f,std::uint32_t now){f.now=now;assert(f.input.tick(now)==PhysicalInputResult::Ok);}
int main(){
    Fixture f;auto& e=f.input;e.io.context=&f;e.io.millis=clock;e.io.pin_mode=mode;e.io.read_pin=read;e.io.attach_interrupt=attach;
    e.services.context=&f;e.services.register_commands=registration;e.services.button_event=forward_button;e.services.rotation_event=forward_rotation;
    f.head.services.context=&f;f.head.services.millis=clock;f.head.services.perform=head_effect;
    f.head.state.menu.enabled=true;f.head.state.menu.selected=3;f.head.state.menu.count=10;f.head.state.menu.visible_rows=3;
    f.buttons.services.context=&f;f.buttons.services.perform=button_effect;
    assert(e.setup()==PhysicalInputResult::Ok&&e.phase==4&&f.registrations==1&&f.interrupts==3);
    for(unsigned value:{2,0,1,3}){f.levels[34]=value>>1;f.levels[35]=value&1;assert(e.interrupt()==PhysicalInputResult::Ok);}
    tick(f,900);assert(e.total==0xfffffffeu&&f.head.state.menu.selected==5&&f.draws==1);
    for(unsigned value:{1,0,2,3}){f.levels[34]=value>>1;f.levels[35]=value&1;assert(e.interrupt()==PhysicalInputResult::Ok);}
    tick(f,901);assert(e.total==0&&f.head.state.menu.selected==3&&f.draws==2);
    f.levels[39]=0;tick(f,1000);f.levels[39]=1;tick(f,1050);tick(f,1150);assert(f.forwarded==0);
    tick(f,1151);assert(f.forwarded==1&&f.clicks==1&&e.button.state.press_count==0);
    f.levels[39]=0;tick(f,2000);tick(f,7000);assert(f.forwarded==1);tick(f,7001);
    assert(f.forwarded==2&&f.clicks==1);tick(f,9000);assert(f.forwarded==2);
    f.levels[39]=1;tick(f,9001);tick(f,9200);assert(f.forwarded==2);
    // The original producer overwrites unread slots. A complete lap is empty.
    const auto position=e.read_index;
    for(unsigned i=0;i<256;++i){f.levels[34]=i&1;assert(e.interrupt()==PhysicalInputResult::Ok);}
    assert(e.write_index==position);tick(f,9300);assert(e.total==0&&f.draws==2);
    // The power producer binds directly to Core's existing one-slot mailbox.
    f.power.io=e.io;f.power.state.pin=32;f.power.state.id=0;assert(f.power.setup()==PhysicalInputResult::Ok);
    f.now=10000;f.levels[32]=0;assert(f.power.tick(f.now)==PhysicalInputResult::Ok);
    f.now=10001;f.levels[32]=1;assert(f.power.tick(f.now)==PhysicalInputResult::Ok);
    f.now=10102;assert(f.power.tick(f.now)==PhysicalInputResult::Ok&&f.core.button_events.pending);
    f.core.services.context=&f;f.core.services.millis=clock;f.core.services.tick=core_tick;f.core.services.perform=core_effect;
    assert(f.core.tick(f.now)==CoreFlowResult::Ok&&!f.core.button_events.pending);
    assert(f.core.commands.pending&&f.core.commands.value.id==CoreCommandId::PowerButton);
    // Service failures do not silently create input or claim setup success.
    PhysicalEncoder missing;assert(missing.setup()==PhysicalInputResult::MissingService);
    assert(missing.interrupt()==PhysicalInputResult::MissingService);
    e.write_index=256;assert(e.interrupt()==PhysicalInputResult::InvalidRingIndex);
    e.write_index=1;e.read_index=0;e.phase=0;assert(e.tick(f.now)==PhysicalInputResult::InvalidPhase);
    e.phase=4;e.read_index=256;assert(e.tick(f.now)==PhysicalInputResult::InvalidRingIndex);
    PendingEvent<CoreButtonEvent> queue;PhysicalButton button(queue);button.state.pin=-1;
    assert(button.setup()==PhysicalInputResult::Ok&&button.tick(0)==PhysicalInputResult::Ok);
    button.state.pin=32;assert(button.tick(0)==PhysicalInputResult::MissingService);
    button.emit(false,2);button.emit(true,9);assert(queue.value.type==0&&queue.value.argument==2);
}
''')
        sources = ['src/physical_inputs.cpp', 'src/head_encoder.cpp', 'src/head_flow.cpp', 'src/head_events.cpp', 'src/core_flow.cpp', 'src/eeprom.cpp']
        subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        '-I', str(ROOT / 'include'), str(source)] + [str(ROOT / p) for p in sources] + ['-o', str(binary)], check=True)
        environment = os.environ.copy(); environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
        subprocess.run([str(binary)], check=True, env=environment)

if __name__ == '__main__': unittest.main()
