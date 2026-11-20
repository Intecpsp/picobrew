import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_display as validation

class DisplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-display-test-')
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads((ROOT/'tests/fixtures/display-original.json').read_text())

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def test_original_framebuffers_geometry_and_io(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.native_case(self.native,row['input']),row['original'])

    def test_recorded_source_and_fixture_identity(self):
        report=json.loads((ROOT/'analysis/display-validation.json').read_text())
        self.assertEqual(report['input_sha256'],validation.IMAGE_SHA)
        self.assertTrue(report['all_results_match'])
        self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(validation.input_cases()))
        self.assertEqual(report['golden_cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/display-original.json'))
        for path,expected in report['source_sha256'].items():
            with self.subTest(path=path):self.assertEqual(validation.digest(ROOT/path),expected)

    def test_missing_io_and_bounded_bitmap_rejection_have_no_effect(self):
        source=Path(self.build.name)/'guards.cpp';executable=Path(self.build.name)/'guards'
        source.write_text(r'''
#include "zseries/display.hpp"
#include <cassert>
using namespace zseries::oem;
int main(){
    DisplayState state;state.pixels.fill(0xab);state.transfer.fill(0xcd);
    const auto pixels=state.pixels,transfer=state.transfer;
    DisplayIo missing;DisplaySetupServices services;
    assert(display_flush(state,missing)==DisplayResult::MissingIo);
    assert(display_clear_and_flush(state,missing)==DisplayResult::MissingIo);
    assert(display_setup(state,missing,services)==DisplayResult::MissingIo);
    unsigned calls=0;DisplayIo io;io.context=&calls;
    io.digital_write=[](void* p,std::uint8_t,std::uint8_t){++*static_cast<unsigned*>(p);};
    io.spi_byte=[](void* p,std::uint8_t){++*static_cast<unsigned*>(p);};
    io.pin_mode=[](void* p,std::uint8_t,std::uint8_t){++*static_cast<unsigned*>(p);};
    assert(display_setup(state,io,services)==DisplayResult::MissingSetupServices);
    assert(calls==0);
    const std::uint8_t data[2]={0x12,0x34};
    DisplayBitmap short_image{4,4,2,DisplayRect{0,0,3,3},data,sizeof data};
    assert(display_bitmap(state,DisplayPoint{0,0},short_image,short_image.bounds)==DisplayResult::InvalidBitmap);
    DisplayBitmap valid{2,2,1,DisplayRect{0,0,1,1},data,sizeof data};
    assert(display_bitmap(state,DisplayPoint{0,0},valid,DisplayRect{0,0,2,1})==DisplayResult::InvalidRectangle);
    assert(display_bitmap(state,DisplayPoint{INT32_MAX,0},valid,valid.bounds)==DisplayResult::InvalidRectangle);
    assert(display_rectangle(state,DisplayRect{20,20,10,30},15,true)==DisplayResult::InvalidRectangle);
    assert(state.pixels==pixels && state.transfer==transfer);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(source),str(ROOT/'src/display.cpp'),'-o',str(executable)],check=True)
        subprocess.run([str(executable)],check=True)

if __name__=='__main__':unittest.main()
