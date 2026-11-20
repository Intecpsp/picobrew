"""Original Head encoder and explicit failed-service behavior."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_encoder_cases as inputs
import head_encoder_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadEncoderTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-encoder-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-encoder-original.json.gz").read_bytes()))

    def test_original_widget_selection_timers_and_assertions(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)
                if "bounded_guard" in original:
                    # Preserve both facts: the original continued into its draw
                    # service, while the reconstruction deliberately stops
                    # before a potentially invalid character-span access.
                    self.assertEqual(original["bounded_guard"]["assertion_line"], 0x481)
                    self.assertNotEqual(original["state"], original["bounded_guard"]["state"])
                    self.assertGreater(len(original["effects"]), len(original["bounded_guard"]["effects"]))

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-encoder-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        compressed = (ROOT / "tests/fixtures/head-encoder-original.json.gz").read_bytes()
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

    def test_assertions_invalid_divisor_and_required_services(self):
        source = Path(self.temporary.name) / "guards.cpp"; binary = source.with_suffix("")
        source.write_text(r'''
#include "zseries/head_encoder.hpp"
#include <cassert>
using namespace zseries::oem;
static bool fail(void*,HeadEncoderEffect,std::uint32_t,std::uint32_t){return false;}
int main(){
    HeadEncoder head;
    assert(head.rotate(0)==HeadEncoderResult::MissingService);
    unsigned assertion_count=0;head.services.context=&assertion_count;
    head.services.assertion=[](void* p,std::uint32_t){++*static_cast<unsigned*>(p);return true;};
    assert(head.rotate(0)==HeadEncoderResult::Ok&&head.state.assertion_line==0x2c2);
    head.state.slideshow.enabled=true;
    assert(head.rotate(1)==HeadEncoderResult::InvalidState);
    head.state.slideshow.enabled=false;head.state.character.enabled=true;head.state.character.count=3;
    assert(head.rotate(5)==HeadEncoderResult::InvalidSelection&&head.state.assertion_line==0x481);
    assert(assertion_count==2);
    assert(head.rotate(1)==HeadEncoderResult::MissingService);
    head.services.perform=fail;
    assert(head.rotate(1)==HeadEncoderResult::ServiceFailure);
    head.state.character.enabled=false;head.state.transient_timer.active=true;
    assert(head.rotate(1)==HeadEncoderResult::MissingService);
    assert(head.rotate(0)==HeadEncoderResult::MissingService&&head.state.assertion_line==0x2c2);
}
''')
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined", "-fno-sanitize-recover=all", "-I" + str(ROOT / "include"),
                        str(ROOT / "src/head_encoder.cpp"), str(ROOT / "src/head_flow.cpp"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
