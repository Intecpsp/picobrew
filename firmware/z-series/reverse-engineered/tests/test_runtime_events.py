"""Original mailbox fields plus the composed Runtime-to-Core-to-Head route."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import run_msp_emulation as common
import runtime_events_cases as inputs
import runtime_events_native as native

ROOT = common.COMPONENT


class RuntimeEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="runtime-events-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.native = native.load_native(cls.temp.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/runtime-events-original.json").read_text())

    def test_original_four_field_mailbox_copy_and_drop_new(self):
        cases = inputs.cases()
        self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, expected in zip(cases, self.fixture["cases"]):
            with self.subTest(words=case["words"]):
                self.assertEqual(native.run(self.native, case), expected)

    def test_composed_core_tick_delivers_notification_to_head(self):
        source = Path(self.temp.name) / "route.cpp"
        binary = Path(self.temp.name) / "route"
        source.write_text(r'''
#include "zseries/runtime_events.hpp"
#include <cassert>
#include <vector>
using namespace zseries::oem;
struct Port { HeadEvents head; unsigned ticks=0; std::vector<unsigned> effects; };
static std::uint32_t clock(void*) { return 1000; }
static bool tick(void* context, CoreTickStage, std::uint32_t) { ++static_cast<Port*>(context)->ticks; return true; }
static bool head_action(void* context, HeadEventEffect effect, std::uint32_t argument) {
    auto& p=*static_cast<Port*>(context); p.effects.push_back(static_cast<unsigned>(effect));p.effects.push_back(argument);return true;
}
static bool action(void* context, const CoreAction& effect, std::uint32_t&) {
    auto& p=*static_cast<Port*>(context);
    assert(effect.effect==CoreEffect::HeadRuntimeEvent&&effect.runtime);
    return p.head.runtime_event(head_runtime_event(*effect.runtime))==HeadEventResult::Ok;
}
int main() {
    EepromData configuration;CoreFlow core(configuration);Port port;
    core.services.context=&port;core.services.millis=clock;core.services.tick=tick;core.services.perform=action;
    port.head.services.context=&port;port.head.services.perform=head_action;
    auto transition=runtime_event_payload(RuntimeEventType::Transition,2,3);
    assert(transition.previous==2&&transition.current==3&&transition.notification==RuntimeNotificationType::None);
    auto update=runtime_event_payload(RuntimeEventType::Update,99,100);
    assert(update.previous==0&&update.current==0&&update.notification==RuntimeNotificationType::None);
    assert(post_runtime_event(core,RuntimeEventType::Notification,1,0));
    assert(!post_runtime_event(core,RuntimeEventType::Transition,3,2));
    assert(core.runtime_events.value.notification==RuntimeNotificationType::T1Dropped);
    assert(core.tick(1000)==CoreFlowResult::Ok);
    assert(!core.runtime_events.pending&&port.ticks!=0);
    assert(port.head.state.notification==RuntimeNotificationType::T1Dropped);
    assert((port.effects==std::vector<unsigned>{0,1,2,1}));
    assert(post_runtime_event(core,RuntimeEventType::Update,0,0));
}
''')
        subprocess.run(["c++", "-std=c++11", "-O2", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "include"),
                        str(ROOT / "src/runtime_events.cpp"), str(ROOT / "src/core_flow.cpp"),
                        str(ROOT / "src/head_events.cpp"), str(ROOT / "src/eeprom.cpp"),
                        str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

    def test_original_ranges_and_source_identity(self):
        proof = json.loads((ROOT / "analysis/runtime-events-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        self.assertEqual(common.shared.digest(ROOT / "tests/fixtures/runtime-events-original.json"), proof["fixture_sha256"])
        self.assertEqual(proof["cases"], len(inputs.cases()))
        self.assertTrue(proof["native_validation"]["all_results_match"])
        for group in ("generator_files", "validated_source_sha256"):
            for path, expected in proof[group].items():
                self.assertEqual(common.shared.digest(ROOT / path), expected, path)
        raw = common.INPUT.read_bytes()
        parsed = common.images.parse_image(raw)
        segments = [dict(address=s["load_address"], offset=s["file_offset"], size=s["size_bytes"]) for s in parsed["segments"]]
        for function in self.fixture["original_functions"]:
            for span in function["ranges"]:
                self.assertEqual(hashlib.sha256(common.read_mapped(raw, segments, span["address"], span["size"])).hexdigest(), span["sha256"])


if __name__ == "__main__":
    unittest.main()
