import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_display_effects as validation

class DisplayEffectsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix='z-display-effects-test-')
        cls.native, _ = validation.load_native(cls.build.name)
        cls.fixture = json.loads((ROOT / 'tests/fixtures/display-effects-original.json').read_text())

    @classmethod
    def tearDownClass(cls): cls.build.cleanup()

    def test_original_gradients_carets_timing_and_complete_spi_output(self):
        self.assertEqual(self.fixture['input_sha256'], validation.IMAGE_SHA)
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native, item['input']), item['original'])

    def test_evidence_identity_and_scope(self):
        report = json.loads((ROOT / 'analysis/display-effects-validation.json').read_text())
        self.assertTrue(report['all_results_match']); self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['input_sha256'], validation.IMAGE_SHA)
        self.assertEqual(report['cases'], len(validation.input_cases()))
        self.assertEqual(report['cases'], len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'], validation.digest(ROOT / 'tests/fixtures/display-effects-original.json'))
        for path, expected in report['source_sha256'].items():
            with self.subTest(path=path): self.assertEqual(validation.digest(ROOT / path), expected)

    def test_unreachable_motion_and_invalid_adapters_fail_before_looping(self):
        source = Path(self.build.name) / 'effect_guards.cpp'; binary = Path(self.build.name) / 'effect_guards'
        source.write_text(r'''
#include "zseries/display_effects.hpp"
#include <cassert>
using namespace zseries::oem;
static void gpio(void*,std::uint8_t,std::uint8_t){}
static void byte(void*,std::uint8_t){}
static void bytes(void*,const std::uint8_t*,std::size_t){}
static void delay(void*,std::uint32_t){}
int main(){
    DisplayState state;state.pixels.fill(0x5a);const auto previous=state.pixels;
    const std::uint8_t pixels[24]={};DisplayBitmap bitmap{8,6,4,{0,0,7,5},pixels,sizeof pixels};
    DisplayIo io;io.digital_write=gpio;io.spi_byte=byte;io.spi_bytes=bytes;
    DisplayDelay wait;wait.milliseconds=delay;
    assert(display_slide(state,io,wait,bitmap,0,{0,0},0,0)==DisplayEffectResult::InvalidMotion);
    assert(display_slide(state,io,wait,bitmap,0,{0,0},3,0)==DisplayEffectResult::InvalidMotion);
    assert(display_slide(state,io,wait,bitmap,0,{-10,0},1,0)==DisplayEffectResult::InvalidMotion);
    assert(display_slide(state,io,wait,bitmap,4,{0,0},1,0)==DisplayEffectResult::InvalidMotion);
    DisplayDelay missing;
    assert(display_slide(state,io,missing,bitmap,0,{0,0},1,0)==DisplayEffectResult::MissingDelay);
    assert(display_caret(state,{0,0},1,15,0)==DisplayEffectResult::InvalidHeight);
    assert(display_caret(state,{0,0},4097,15,0)==DisplayEffectResult::InvalidHeight);
    DisplayAnimation invalid{1,0,0,nullptr};DisplayAnimationState motion;motion.animation=&invalid;
    assert(display_animation_tick(state,motion,io,2)==DisplayEffectResult::InvalidAnimation);
    motion.animation=nullptr;motion.dirty=true;DisplayIo no_io;
    assert(display_animation_tick(state,motion,no_io,2)==DisplayEffectResult::MissingIo);
    assert(motion.dirty);assert(state.pixels==previous);
}
''')
        subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'include'), str(source),
            str(ROOT / 'src/display.cpp'), str(ROOT / 'src/display_effects.cpp'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True, timeout=5)

if __name__ == '__main__': unittest.main()
