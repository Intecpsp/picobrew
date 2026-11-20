"""Replay original Head runtime alarm and encoder-button event behavior."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import head_events_cases as inputs
import head_events_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-events-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/head-events-original.json").read_text())

    def test_all_original_runtime_and_button_event_effects(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-events-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        self.assertEqual(common.shared.digest(ROOT / "tests/fixtures/head-events-original.json"), proof["fixture_sha256"])
        self.assertEqual(proof["cases"], len(inputs.cases()))
        self.assertTrue(proof["native_validation"]["all_results_match"])
        for group in ["generator_files", "validated_source_sha256"]:
            for filename, expected in proof[group].items():
                self.assertEqual(common.shared.digest(ROOT / filename), expected, filename)
        raw = common.INPUT.read_bytes()
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"])
                    for s in parsed["segments"]]
        import hashlib
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])

    def test_required_effect_services_propagate_failure(self):
        source, binary = Path(self.temporary.name) / "guards.cpp", Path(self.temporary.name) / "guards"
        source.write_text(r'''
#include "zseries/head_events.hpp"
#include <cassert>
using namespace zseries::oem;
static unsigned calls;
static bool fail(void*,HeadEventEffect,std::uint32_t){++calls;return false;}
int main(){
    HeadEvents head;HeadRuntimeEvent event;event.type=RuntimeEventType::Notification;
    event.notification=RuntimeNotificationType::T1Dropped;
    assert(head.runtime_event(event)==HeadEventResult::MissingService);
    assert(head.state.notification==RuntimeNotificationType::None);
    head.services.perform=fail;
    assert(head.runtime_event(event)==HeadEventResult::ServiceFailure);
    assert(calls==1&&head.state.notification==RuntimeNotificationType::None);
    event.type=RuntimeEventType::Update;
    assert(head.runtime_event(event)==HeadEventResult::ServiceFailure&&calls==2);
    assert(head.button_event(false)==HeadEventResult::ServiceFailure&&calls==3);
    assert(head.button_event(true)==HeadEventResult::Ok&&calls==3);
    head.state.encoder_press_hijacked=true;
    assert(head.button_event(true)==HeadEventResult::Ok&&head.state.encoder_press_hijacked);
    assert(head.button_event(false)==HeadEventResult::ServiceFailure&&!head.state.encoder_press_hijacked);
    event.type=RuntimeEventType::Transition;
    assert(head.runtime_event(event)==HeadEventResult::Ok&&calls==4);
}
''')
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "include"),
                        str(ROOT / "src/head_events.cpp"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
