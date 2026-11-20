"""Original numeric Head handlers and explicit failed-service behavior."""
import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_numeric_cases as inputs
import head_numeric_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadNumericTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-numeric-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-numeric-original.json.gz").read_bytes()))

    def test_original_numeric_commands_parameters_and_assertions(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-numeric-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        compressed=(ROOT / "tests/fixtures/head-numeric-original.json.gz").read_bytes()
        self.assertEqual(hashlib.sha256(compressed).hexdigest(),proof["compressed_fixture_sha256"])
        self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(),proof["fixture_sha256"])
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

    def test_invalid_quantity_and_required_effect_services(self):
        source = Path(self.temporary.name) / "guards.cpp"; binary = source.with_suffix("")
        source.write_text(r'''
#include "zseries/head_numeric_inputs.hpp"
#include <cassert>
using namespace zseries::oem;
static unsigned calls,assertions;
static bool diagnose(void*,std::uint32_t){++assertions;return true;}
static bool fail(void*,HeadNumericEffect,const CoreCommand*){++calls;return false;}
int main(){
    HeadNumericInputs head;
    assert(head.temperature(50)==HeadNumericResult::MissingService);
    assert(head.duration(60)==HeadNumericResult::MissingService);
    assert(head.location(2)==HeadNumericResult::MissingService);
    head.services.perform=fail;
    assert(head.temperature(50)==HeadNumericResult::ServiceFailure&&calls==1);
    assert(head.duration(60)==HeadNumericResult::ServiceFailure&&calls==2);
    assert(head.location(2)==HeadNumericResult::ServiceFailure&&calls==3);
    head.state.manual.byte_00=true;
    assert(head.location(3)==HeadNumericResult::ServiceFailure&&calls==4);
    assert(head.state.manual.location==3&&head.state.manual.bytes_38[0]==1);
    const auto previous=head.state.manual.temperature;
    assert(head.quantity(HeadQuantityId::CoffeeBatchSize,{9,Unit::Liter})==HeadNumericResult::MissingService);
    assert(calls==4&&head.state.assertion_line==0x2ba);
    head.services.assertion=diagnose;
    assert(head.quantity(HeadQuantityId::CoffeeBatchSize,{9,Unit::Liter})==HeadNumericResult::ServiceFailure);
    assert(calls==5&&assertions==1);
    assert(head.state.manual.temperature.value==previous.value&&head.state.manual.temperature.unit==previous.unit);
    head.state.manual.byte_00=false;head.state.parameters.id=ZProgramId::CoffeeHot;
    assert(head.quantity(HeadQuantityId::StepDrain,{9,Unit::Second})==HeadNumericResult::ServiceFailure);
    assert(calls==6&&assertions==2&&head.state.assertion_line==0x349);
    head.state.abv_pending=true;head.state.ibu_pending=true;
    assert(head.strength(7)==HeadNumericResult::ServiceFailure&&calls==7);
    assert(head.state.parameters.abv==7&&!head.state.abv_pending&&head.state.ibu_pending);
}
''')
        subprocess.run(["c++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-I" + str(ROOT / "include"),
                        str(ROOT / "src/head_numeric_inputs.cpp"), str(source), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
