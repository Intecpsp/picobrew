"""Original Head network events and explicit failed-service behavior."""
import hashlib
import gzip
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import head_network_cases as inputs
import head_network_native as native
import run_msp_emulation as common

ROOT = native.ROOT


class HeadNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="z119-head-network-")
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.native = native.load_native(cls.temporary.name)
        cls.fixture = json.loads(gzip.decompress((ROOT / "tests/fixtures/head-network-original.json.gz").read_bytes()))

    def test_original_network_routes_and_coffee_parameters(self):
        cases = inputs.cases(); self.assertEqual(len(cases), len(self.fixture["cases"]))
        for case, original in zip(cases, self.fixture["cases"]):
            with self.subTest(case=case):
                self.assertEqual(native.run(self.native, case), native.compared(original))
                self.assertEqual(original["event_pending"], 0)

    def test_original_instruction_and_reconstructed_source_provenance(self):
        proof = json.loads((ROOT / "analysis/head-network-validation.json").read_text())
        self.assertEqual(common.shared.digest(common.INPUT), self.fixture["input_sha256"])
        compressed=(ROOT / "tests/fixtures/head-network-original.json.gz").read_bytes()
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

    def test_shared_parameters_quantity_failure_and_required_services(self):
        source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('')
        source.write_text(r'''
#include "zseries/head_network_events.hpp"
#include <cassert>
#include <vector>
using namespace zseries::oem;
static HeadNetworkEffect failure=HeadNetworkEffect::PostCoreCommand;
static std::vector<HeadNetworkEffect> calls;
static bool perform(void*,const HeadNetworkAction& action){calls.push_back(action.effect);return action.effect!=failure;}
int main(){
    ProgramParameters shared;HeadNetworkEvents head(shared);CoreWifiEvent wifi;wifi.type=WifiEventType::Error;
    head.core=CoreStateId::Running;assert(head.wifi(wifi)==HeadNetworkResult::Ok);
    head.core=CoreStateId::Idle;assert(head.wifi(wifi)==HeadNetworkResult::MissingService);
    head.services.perform=perform;HeadAzureEvent cloud;cloud.type=AzureEventType::ExitStartProgram;
    assert(head.azure(cloud)==HeadNetworkResult::ServiceFailure);
    assert((calls==std::vector<HeadNetworkEffect>{HeadNetworkEffect::PostCoreCommand}));
    calls.clear();cloud.type=AzureEventType::DisplayStartProgram;cloud.program=ZProgramId::CoffeeHot;
    shared.water={99,Unit::Liter};cloud.parameters.water={7,Unit::Gram};
    assert(head.azure(cloud)==HeadNetworkResult::InvalidQuantity&&head.assertion_line==0x7d);
    assert(shared.id==ZProgramId::CoffeeHot&&shared.water.value==99);
    assert((calls==std::vector<HeadNetworkEffect>{HeadNetworkEffect::ResetScreen,HeadNetworkEffect::AppendCommand}));
    cloud.parameters.water={7,Unit::Liter};cloud.parameters.time={123,Unit::None};cloud.parameters.intensity={8,Unit::None};
    cloud.parameters.temperature={12,Unit::Gram};
    assert(head.azure(cloud)==HeadNetworkResult::InvalidQuantity);
    assert(shared.water.value==7&&shared.time.value==123&&shared.time.unit==Unit::Second&&shared.intensity.unit==Unit::Intensity);
    cloud.parameters.temperature={100,Unit::Celsius};
    assert(head.azure(cloud)==HeadNetworkResult::Ok&&shared.temperature.value==212&&shared.temperature.unit==Unit::Fahrenheit);
    calls.clear();head.core=CoreStateId::ConnectingWiFi;wifi.type=WifiEventType::ScanCompleted;
    assert(head.wifi(wifi)==HeadNetworkResult::Ok&&calls.empty());
}
''')
        sources=['src/head_network_events.cpp','src/quantity.cpp','src/enums.cpp','src/ui_catalog.cpp']
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include')]+[str(ROOT/p) for p in sources]+[str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)

if __name__=='__main__':unittest.main()
