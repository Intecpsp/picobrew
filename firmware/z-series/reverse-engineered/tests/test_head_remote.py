"""Remote session routing completes the original assertion and payload paths."""
import gzip,hashlib,json,subprocess,tempfile,unittest
from pathlib import Path
import head_remote_cases as inputs,head_remote_native as native,run_msp_emulation as common
ROOT=native.ROOT
class HeadRemoteTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.temporary=tempfile.TemporaryDirectory(prefix='head-remote-');cls.addClassCleanup(cls.temporary.cleanup);cls.native=native.load_native(cls.temporary.name);cls.fixture=json.loads(gzip.decompress((ROOT/'tests/fixtures/head-remote-original.json.gz').read_bytes()))
 def test_remote_selection_payloads_and_returning_assertions(self):
  self.assertEqual(len(inputs.cases()),len(self.fixture['cases']))
  for case,expected in zip(inputs.cases(),self.fixture['cases']):
   with self.subTest(case=case):self.assertEqual(native.run(self.native,case),native.compared(expected));self.assertEqual(expected['use_azure'],0)
 def test_original_and_source_identity(self):
  proof=json.loads((ROOT/'analysis/head-remote-validation.json').read_text());compressed=(ROOT/'tests/fixtures/head-remote-original.json.gz').read_bytes();self.assertEqual(hashlib.sha256(compressed).hexdigest(),proof['compressed_fixture_sha256']);self.assertEqual(hashlib.sha256(gzip.decompress(compressed)).hexdigest(),proof['fixture_sha256']);self.assertEqual(common.shared.digest(common.INPUT),proof['input_sha256'])
  for group in ['generator_files','validated_source_sha256']:
   for path,expected in proof[group].items():self.assertEqual(common.shared.digest(ROOT/path),expected,path)
  raw=common.INPUT.read_bytes();segments=[dict(address=s['load_address'],offset=s['file_offset'],size=s['size_bytes'])for s in common.images.parse_image(raw)['segments']]
  for function in self.fixture['original_functions']:
   for span in function['ranges']:self.assertEqual(hashlib.sha256(common.read_mapped(raw,segments,span['address'],span['size'])).hexdigest(),span['sha256'])
  self.assertTrue({'0x401021b8','0x400db014'}.issubset({f['address']for f in self.fixture['original_functions']}))
 def test_required_bindings_and_owned_payload_lifetime(self):
  source=Path(self.temporary.name)/'guards.cpp';binary=source.with_suffix('');source.write_text(r'''
#include "zseries/head_remote_session.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
struct Owner{PendingEvent<HeadUiCoreEnvelope> queue;unsigned assertions=0;};
int main(){AzureCommand value;value.id=CoreCommandId::AzureSessionLoad;value.session.program=static_cast<ZProgramId>(24);value.session.type=SessionType::BrewZymatic;value.session.recipe_id=77;std::strcpy(value.session.name.data(),"Copied recipe");HeadRemoteServices service;Owner owner;service.context=&owner;
 assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::MissingService);service.millis=[](void*){return 10000u;};assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::MissingService);
 service.post_core=[](void* p,const HeadUiCoreEnvelope& envelope){static_cast<Owner*>(p)->queue.put(envelope);return true;};assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::Ok);value.session.name.fill(0);assert(owner.queue.value.command.detail==&owner.queue.value.data);assert(owner.queue.value.data.recipe.id==77&&!std::strcmp(owner.queue.value.data.recipe.name.data(),"Copied recipe"));
 value.session.recipe_id=88;assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::Ok&&owner.queue.value.data.recipe.id==77);
 value.session.program=ZProgramId::None;assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::MissingService);service.assertion=[](void* p,const char*,std::uint32_t,const char*){++static_cast<Owner*>(p)->assertions;return true;};service.discard=[](void*){return false;};assert(head_remote_session(value,0,10000,service)==HeadRemoteResult::ServiceFailure&&owner.assertions==2);
}
''');subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),str(ROOT/'src/head_remote_session.cpp'),str(ROOT/'src/quantity.cpp'),str(ROOT/'src/enums.cpp'),str(source),'-o',str(binary)],check=True);subprocess.run([str(binary)],check=True)
if __name__=='__main__':unittest.main()
