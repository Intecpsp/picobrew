"""Original Head UI command dispatch and explicit failed-service behavior."""
import hashlib
import gzip
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_ui_cases as inputs
import head_ui_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadUiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-ui-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-ui-original.json.gz").read_bytes()))

    def test_every_original_ui_command_and_payload(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["state"][8], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-ui-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        compressed=(ROOT / "tests/fixtures/head-ui-original.json.gz").read_bytes()
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

    def test_returning_assertions_do_not_suppress_commands(self):
        cases=inputs.cases()
        self.assertTrue(set(range(176)).issubset({case['command'] for case in cases}))
        for case,original in zip(cases,self.fixture['cases']):
            line=original['state'][6]
            if not line:continue
            self.assertIn(line,[0x37,0x1a6,0x1a8,0x8b4])
            count=2 if case['command'] in [8,9,10,11] and not case['flags']&4 else 1
            self.assertEqual(original['state'][7],case['state'][14]+count)
            if case['command'] in [4,8,9,10,11,21,40]:
                self.assertTrue(any(effect['kind']==2 for effect in original['effects']))
            if line==0x37:self.assertEqual(original['state'][:3],[1,0,case['state'][13]])

    def test_owned_core_envelope_and_required_services(self):
        source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('')
        source.write_text(r'''
#include "zseries/head_ui_commands.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
static bool fail(void*,const HeadUiAction&){return false;}
static bool pass(void*,const HeadUiAction&){return true;}
static bool assertion(void*,std::uint32_t){return false;}
int main(){
    HeadNumericState numeric;HeadTextState text;HeadTimer timer;HeadUiCommands head(numeric,text,timer);
    PendingEvent<HeadUiEvent> events;events.pending=true;events.value.command=UiCommand::CoffeeProgramLoad;
    assert(head.consume(events)==HeadUiResult::MissingService&&!events.pending);
    head.services.perform=fail;assert(head.consume(events)==HeadUiResult::ServiceFailure);
    head.services.perform=pass;assert(head.consume(events)==HeadUiResult::MissingService&&head.state.assertion_line==0x8b4);
    head.services.assertion=assertion;assert(head.consume(events)==HeadUiResult::AssertionFailure);
    CoreCommand command;HeadUiCommandData data;data.flags=4;data.recipe.id=1234;data.search[0]='a';data.has_search=true;
    HeadUiCoreEnvelope original(command,&data);PendingEvent<HeadUiCoreEnvelope> queue;assert(queue.put(original));
    original.data.recipe.id=999;data.search[0]='x';assert(queue.value.data.recipe.id==1234&&queue.value.data.search[0]=='a');
    assert(queue.value.command.detail==&queue.value.data&&queue.value.command.detail!=original.command.detail);
    HeadUiCoreEnvelope consumed;assert(queue.take(consumed));assert(consumed.command.detail==&consumed.data);
    assert(consumed.data.recipe.id==1234);assert(!queue.take(consumed));
}
''')
        sources=['src/head_ui_commands.cpp','src/enums.cpp','src/ui_catalog.cpp']
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+[str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
