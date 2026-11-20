"""Original Head text handler and explicit failed-service behavior."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_text_cases as inputs
import head_text_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-text-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads((ROOT / "tests/fixtures/head-text-original.json").read_text())

    def test_original_text_routes_and_buffer_copy(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-text-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        self.assertEqual(common.shared.digest(ROOT / "tests/fixtures/head-text-original.json"), proof["fixture_sha256"])
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

    def test_unterminated_input_and_required_post_service(self):
        source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('')
        source.write_text(r'''
#include "zseries/head_text_input.hpp"
#include <cassert>
using namespace zseries::oem;
static unsigned calls;
static bool fail(void*,HeadTextEffect,const HeadTextCommand*){++calls;return false;}
int main(){
    HeadTextInput head;std::array<char,65> value{};value.fill('x');head.state.awaiting_wifi_password=true;
    assert(head.input(UiCommand::PicoPakRecipeInput,value)==HeadTextResult::UnterminatedText);
    assert(head.state.awaiting_wifi_password&&head.state.credentials.password[0]==0);
    value[64]=0;
    assert(head.input(UiCommand::None,value)==HeadTextResult::MissingService);
    assert(!head.state.awaiting_wifi_password&&head.state.credentials.password[63]=='x'&&head.state.credentials.password[64]==0);
    head.services.perform=fail;head.state.awaiting_wifi_password=true;
    assert(head.input(UiCommand::ZKitRecipeListGetSearch,value)==HeadTextResult::ServiceFailure&&calls==1);
    assert(!head.state.awaiting_wifi_password&&head.state.kit_length==0);
    assert(head.input(UiCommand::ZKitRecipeListGetSearch,value)==HeadTextResult::ServiceFailure&&calls==2);
    assert(head.state.kit_length==16&&head.state.kit_truncated&&head.state.kit_code[16]==0);
    value.fill(0);value[0]='7';
    assert(head.input(UiCommand::ZKitRecipeListGetSearch,value)==HeadTextResult::ServiceFailure&&calls==3);
    assert(head.state.kit_length==1&&!head.state.kit_truncated&&head.state.kit_code[1]==0);
    assert(head.input(UiCommand::PicoPakRecipeInput,value)==HeadTextResult::ServiceFailure&&calls==4);
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/head_text_input.cpp'),str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
