import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import validate_core_event as validation


class CoreEventTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build=tempfile.TemporaryDirectory(prefix='z-core-event-test-')
        cls.native,_=validation.load_native(cls.build.name)
        cls.fixture=json.loads((ROOT/'tests/fixtures/core-event-original.json').read_text())

    @classmethod
    def tearDownClass(cls):cls.build.cleanup()

    def test_original_initialization_reset_and_complete_mailbox_payload(self):
        self.assertEqual(self.fixture['input_sha256'],validation.IMAGE_SHA)
        self.assertEqual([row['input'] for row in self.fixture['cases']],validation.input_cases())
        for row in self.fixture['cases']:
            with self.subTest(case=row['input']['id']):
                self.assertEqual(validation.native_case(self.native,row['input']),row['original'])

    def test_evidence_and_target_source_identity(self):
        report=json.loads((ROOT/'analysis/core-event-validation.json').read_text())
        self.assertTrue(report['all_results_match']);self.assertFalse(report['hardware_tested'])
        self.assertEqual(report['cases'],len(self.fixture['cases']))
        self.assertEqual(report['golden_fixture_sha256'],validation.digest(ROOT/'tests/fixtures/core-event-original.json'))
        for path,expected in report['source_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected,path)
        target=json.loads((ROOT/'analysis/core-event-target-build.json').read_text())
        self.assertFalse(target['complete_application_linked']);self.assertFalse(target['hardware_tested'])
        for row in target['sources']:self.assertEqual(validation.digest(ROOT/row['source']),row['source_sha256'])
        for path,expected in target['owned_header_sha256'].items():self.assertEqual(validation.digest(ROOT/path),expected)

    def test_notification_owns_strings_and_preserves_pending_event(self):
        source=Path(self.build.name)/'event_ownership.cpp';binary=Path(self.build.name)/'event_ownership'
        source.write_text(r'''
#include "zseries/program_list.hpp"
#include <cassert>
#include <cstring>
using namespace zseries::oem;
int main(){
    CoreEvent current,received;CoreEventMailbox mailbox;
    current.type=CoreEventType::RecipeRefListLoaded;current.count=1;current.offset=20;current.total=21;
    current.argument=10;current.second_argument=20;current.recipes[0].id=123;
    std::memcpy(current.recipes[0].name.data(),"Owned name",11);current.recipes[0].name_length=10;
    assert(mailbox.put(current));current.recipes[0].name[0]='X';
    LocalRecipePublication publication(current,mailbox);const auto notification=publication.notification();
    notification.ready(notification.context);
    assert(current.type==CoreEventType::ProgramLoaded&&current.count==0&&current.offset==0&&current.total==0);
    assert(current.argument==10&&current.second_argument==20&&current.recipes[0].id==123);
    assert(mailbox.take(received));assert(received.type==CoreEventType::RecipeRefListLoaded);
    assert(received.count==1&&received.offset==20&&std::strcmp(received.recipes[0].name.data(),"Owned name")==0);
    assert(!mailbox.take(received));notification.ready(notification.context);
    assert(mailbox.take(received)&&received.type==CoreEventType::ProgramLoaded);
    assert(received.source==CoreCommandId::None&&received.recipes[0].name[0]=='X');
    current.initialize();assert(current.argument==10&&current.second_argument==20);
    for(const auto& ref:current.recipes){assert(ref.id==0xffffffffu&&ref.kind==RecipeKind::Unknown);
        assert(ref.name[0]==0&&ref.name_length==0&&!ref.name_overflow&&!ref.name_frozen);}
}
''')
        subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror','-pedantic','-I',str(ROOT/'include'),
            str(source),str(ROOT/'src/program_list.cpp'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True,timeout=5)


if __name__=='__main__':unittest.main()
