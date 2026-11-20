"""Original Head Core/Still consumption and explicit failed-service behavior."""
import hashlib
import gzip
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_core_cases as inputs
import head_core_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-core-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-core-original.json.gz").read_bytes()))

    def test_original_core_and_still_consumption(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-core-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        compressed=(ROOT / "tests/fixtures/head-core-original.json.gz").read_bytes()
        self.assertEqual(hashlib.sha256(compressed).hexdigest(), proof["compressed_fixture_sha256"])
        self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(), proof["fixture_sha256"])
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

    def test_returning_assertion_continuation_and_empty_retention(self):
        cases=inputs.cases()
        asserted=[(case,original) for case,original in zip(cases,self.fixture['cases']) if original['assertion_line']]
        self.assertTrue(asserted)
        for case,original in asserted:
            self.assertEqual(original['assertion_counter'],(case['state'][15]+1)&0xffffffff)
            self.assertIn(original['assertion_line'],[0x37,0x102])
            if original['assertion_line']==0x37:
                self.assertEqual(original['state'][:3],[1,0,case['state'][14]])
            else:
                self.assertEqual(original['state'][3],1)
                self.assertGreater(original['effects'][-1]['first'],100)
        empty=[original for case,original in zip(cases,self.fixture['cases']) if not case['pending'] and case['operation']==2]
        self.assertTrue(any(original['still']!=[0,0] for original in empty))

    def test_required_presentation_and_assertion_services(self):
        source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('')
        source.write_text(r'''
#include "zseries/head_core_events.hpp"
#include <cassert>
using namespace zseries::oem;
static bool fail(void*,const HeadCoreAction&,std::uint32_t&){return false;}
static bool pass(void*,const HeadCoreAction&,std::uint32_t&){return true;}
static bool assertion(void*,std::uint32_t){return false;}
int main(){
    HeadCoreEvents head;CoreEventMailbox mailbox;mailbox.pending=true;mailbox.value.type=CoreEventType::Loading;
    assert(head.consume(mailbox)==HeadCoreResult::MissingService&&!mailbox.pending);
    head.services.perform=fail;assert(head.consume(mailbox)==HeadCoreResult::ServiceFailure);
    head.services.perform=pass;mailbox.value.type=CoreEventType::StateTransition;mailbox.value.argument=8;
    head.observed.download_received=2;head.observed.download_total=1;
    assert(head.consume(mailbox)==HeadCoreResult::MissingService&&!head.state.update_progress_visible);
    head.services.assertion=assertion;assert(head.consume(mailbox)==HeadCoreResult::AssertionFailure);
    PendingEvent<CoreStillEvent> still;CoreStillEvent a;a.type=StillEventId::None;a.state=PicoStillStateId::Unknown;
    assert(still.put(a));assert(!still.put(a));consume_head_still_event(still);assert(!still.pending);
}
''')
        sources=['src/head_core_events.cpp','src/enums.cpp','src/ui_catalog.cpp']
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+[str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
