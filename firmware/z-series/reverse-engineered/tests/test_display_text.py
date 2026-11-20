import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import extract_display_fonts
import validate_display_text as validation

class DisplayTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-display-text-test-');cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads((ROOT/'tests/fixtures/display-text-original.json').read_text())
    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def test_original_text_metrics_utf8_and_framebuffers(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        for item in self.fixture['cases']:
            with self.subTest(case=item['input']['id']):
                self.assertEqual(validation.native_case(self.native,item['input']),item['original'])

    def test_all_font_descriptors_and_pixels_are_exact(self):
        source,report=extract_display_fonts.outputs()
        self.assertEqual((ROOT/'src/display_fonts.cpp').read_text(),source)
        self.assertEqual((ROOT/'analysis/display-fonts.json').read_text(),report)
        fonts=json.loads(report)['fonts'];self.assertEqual(len(fonts),5)
        self.assertEqual(sum(x['pixels_bytes'] for x in fonts),94403)

    def test_evidence_identity_and_scope(self):
        report=json.loads((ROOT/'analysis/display-text-validation.json').read_text())
        self.assertEqual(report['input_sha256'],validation.IMAGE_SHA);self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()));self.assertEqual(report['cases'],len(self.fixture['cases']))
        self.assertEqual(report['bounded_assertion_cases'],sum(bool(x['original']['error']) for x in self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/display-text-original.json'))
        for path,expected in report['source_sha256'].items():
            with self.subTest(path=path):self.assertEqual(validation.digest(ROOT/path),expected)

    def test_invalid_text_font_and_bounds_are_bounded(self):
        source=Path(self.build.name)/'text_guards.cpp';executable=Path(self.build.name)/'text_guards'
        source.write_text(r'''
#include "zseries/display_text.hpp"
#include <cassert>
using namespace zseries::oem;
int main(){
    DisplayState state;state.pixels.fill(0x5a);const auto previous=state.pixels;
    DisplayTextContext context;DisplayTextOptions options;
    assert(display_text(state,context,"x",2,options)==DisplayTextResult::InvalidFont);
    assert(display_font(static_cast<DisplayFontId>(99))==nullptr);
    const auto font=display_font(DisplayFontId::Font6x8);assert(font);
    assert(display_text_font(context,*font)==DisplayTextResult::Ok);
    assert(display_text_bounds(context,DisplayRect{3,0,2,5})==DisplayTextResult::InvalidRegion);
    assert(display_text(state,context,nullptr,1,options)==DisplayTextResult::MissingText);
    const char unterminated[2]={'a','b'};
    assert(display_text(state,context,unterminated,2,options)==DisplayTextResult::UnterminatedText);
    options.characters=1000;options.alignment=DisplayTextAlignment::Right;
    assert(display_text(state,context,"x",2,options)==DisplayTextResult::InvalidPosition);
    DisplayUtf8 decoder;assert(display_utf8_initialize(decoder,"\xc3",2)==DisplayTextResult::Ok);
    std::uint32_t codepoint=0;assert(display_utf8_next(decoder,codepoint)==DisplayTextResult::TruncatedUtf8);
    assert(decoder.cursor==1);
    assert(display_utf8_initialize(decoder,"\xc0\x80",3)==DisplayTextResult::Ok);
    assert(display_utf8_next(decoder,codepoint)==DisplayTextResult::OverlongUtf8);
    assert(state.pixels==previous);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(source),
            str(ROOT/'src/display.cpp'),str(ROOT/'src/display_fonts.cpp'),str(ROOT/'src/display_text.cpp'),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)

if __name__=='__main__':unittest.main()
