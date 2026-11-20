"""Original ScreenLibrary tick and explicit failed-service behavior."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import ui_screen_tick_cases as inputs
import ui_screen_tick_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class UiScreenTickTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-ui-screen-tick-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/ui-screen-tick-original.json").read_text())

    def test_original_periodic_gate_order_and_deadlines(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/ui-screen-tick-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        self.assertEqual(common.shared.digest(ROOT / "tests/fixtures/ui-screen-tick-original.json"), proof["fixture_sha256"])
        self.assertEqual(proof["cases"], len(inputs.cases()))
        self.assertTrue(proof["native_validation"]["all_results_match"])
        for group in ["generator_files", "validated_source_sha256"]:
            for path, expected in proof[group].items():
                self.assertEqual(common.shared.digest(ROOT / path), expected, path)
        raw = common.INPUT.read_bytes(); parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in parsed["segments"]]
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])

    def test_required_services_failure_order_and_frame_guard(self):
        source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('')
        source.write_text(r'''
#include "zseries/ui_screen_tick.hpp"
#include <cassert>
using namespace zseries::oem;
static UiScreenTickEffect failed=UiScreenTickEffect::RuntimeStateDraw;
static bool perform(void*,UiScreenTickEffect e,std::uint32_t,std::uint32_t){return e!=failed;}
int main(){
    UiScreenTick tick;
    assert(tick.tick(0)==UiScreenTickResult::MissingService);
    tick.state.combined=true;
    assert(tick.tick(0)==UiScreenTickResult::MissingService);
    tick.state.combined=false;tick.services.perform=perform;
    tick.state.runtime_state.enabled=true;
    assert(tick.tick(1)==UiScreenTickResult::ServiceFailure);
    assert(tick.state.runtime_state.deadline==0);
    tick.state.runtime_state.enabled=false;tick.state.waiting.enabled=true;tick.state.waiting.animate=true;
    assert(tick.tick(1)==UiScreenTickResult::InvalidFrameCount);
    assert(tick.state.waiting.deadline==501);
    tick.state.waiting.deadline=0;tick.state.waiting.frame_count=4;failed=UiScreenTickEffect::WaitingDraw;
    assert(tick.tick(1)==UiScreenTickResult::ServiceFailure);
    assert(tick.state.waiting.frame==1&&tick.state.waiting.deadline==501);
    tick.state.waiting.enabled=false;tick.state.paragraph_caret.enabled=true;tick.state.paragraph_caret.interval_ms=500;
    failed=UiScreenTickEffect::ScrollCaretDraw;
    assert(tick.tick(1)==UiScreenTickResult::ServiceFailure&&tick.state.paragraph_caret.deadline==0);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/ui_screen_tick.cpp'),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
