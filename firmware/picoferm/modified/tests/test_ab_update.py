"""Exercise the real receiver and bootstrap against NOR flash, including torn writes."""
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
COMMON = ROOT.parents[1] / 'common'
AB = ROOT / 'platform/ab'
RECOVERY = ROOT.parents[1] / 'pico-s-pro/modified/recovery/recovery'
sys.path.insert(0, str(ROOT/'tools'))
from build_ab import BANKS, BANK_SIZE, VERSION, bank_record, component
UID = 'aabbccddeeff'
TRAILER = 0x100000

def app(bank, version, grain=0):
    identity = ('PicoFerm-AB1:'+version).encode().ljust(32,b'\0')
    return component(0x40100004, [
        dict(address=0x40200010+BANKS[bank],data=identity+b'\x97'*(grain*4)),
        dict(address=0x40100000,data=b'\xcc'*16),
        dict(address=0x3ffe8000,data=b'\x33'*8),dict(address=0x3ffe8010,data=b'\x42'*12)])

class FirmwareUpdate(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory()
        work=Path(cls.temp.name)
        directory=os.getenv('PICOFERM_AB_BUILD')
        cls.build=Path(directory) if directory else None
        cls.loader=(cls.build/'bootstrap.bin').read_bytes() if cls.build else b'ab-loader'*16
        (work/'bootstrap_identity.h').write_text(
            '#define FERM_LOADER_LENGTH %du\nstatic const unsigned char ferm_loader_sha[32]={%s};\n' %
            (len(cls.loader),','.join(map(str,hashlib.sha256(cls.loader).digest()))))
        objects=[]
        for source in (AB/'boot.c',RECOVERY/'sha256.c',AB/'transfer.cpp',ROOT/'tests/ab_driver.cpp'):
            obj=work/(source.stem+'.o');cpp=source.suffix=='.cpp'
            subprocess.run([('c++' if cpp else 'cc'),('-std=c++11' if cpp else '-std=c99'),
                '-O2','-fPIC','-Wall','-Wextra','-Werror','-I'+str(AB),'-I'+str(RECOVERY),
                '-I'+str(work),'-I'+str(COMMON/'include'),'-I'+str(COMMON/'src'),'-c',source,'-o',obj],check=True)
            objects.append(obj)
        lib=work/'ab.so';subprocess.run(['c++','-shared',*objects,'-o',lib],check=True)
        cls.lib=C.CDLL(str(lib))
        P=C.c_void_p;U=C.c_uint;I=C.c_int;B=C.c_char_p
        def bind(name,args,result):
            fn=getattr(cls.lib,name);fn.argtypes=args;fn.restype=result
        bind('start',[],P);bind('destroy',[P],None);bind('boot',[P],I)
        bind('seed',[P,U,B,U],None);bind('copyout',[P,U,P,U],None)
        bind('fault',[P,I,I],None);bind('writes',[P],I);bind('request',[P,I,B,B,I,I,P],I)

    @classmethod
    def tearDownClass(cls):cls.temp.cleanup()
    def setUp(self):
        self.flash=self.lib.start()
        self.initial=app(0,'1.2.6')
        self.image=app(1,VERSION,1100)
        self.seed(0,self.loader);self.seed(BANKS[0],self.initial)
        self.seed(4096,bank_record(0,self.initial))
        self.seed(TRAILER,b'private-key/history/settings'*10)
        self.assertEqual(self.lib.boot(self.flash),0)
    def tearDown(self):self.lib.destroy(self.flash)
    def seed(self,address,data):self.lib.seed(self.flash,address,data,len(data))
    def snapshot(self,start,size):
        data=C.create_string_buffer(size);self.lib.copyout(self.flash,start,data,size);return bytes(data)
    def call(self,path,body=None,auth=1,idle=1):
        out=C.create_string_buffer(4096)
        state=self.lib.request(self.flash,body is not None,path.encode(),json.dumps(body).encode(),auth,idle,out)
        return state,json.loads(out.value or '{}')
    def begin(self,version=VERSION):
        self.assertEqual(self.call('/enter',{'device_uid':UID})[0],202)
        return self.call('/begin',dict(device_uid=UID,model='picoferm',version=version,size=len(self.image),
                                      sha256=hashlib.sha256(self.image).hexdigest()))
    def transfer(self):
        status,tr=self.begin()
        if status!=200:return False
        for pos in range(0,len(self.image),1024):
            status,_=self.call('/chunk',dict(session=tr['session'],offset=pos,data=self.image[pos:pos+1024].hex()))
            if status!=200:return False
        return self.call('/finish',dict(session=tr['session']))[0]==202
    def test_success_and_rollback(self):
        self.assertTrue(self.transfer())
        self.assertEqual(self.lib.boot(self.flash),1)
        status=self.call('')[1]
        self.assertIs(status['trial'],True)
        self.assertEqual((status['running_bank'],status['image_size'],status['image_sha256']),
                         (1,len(self.image),hashlib.sha256(self.image).hexdigest()))
        self.assertEqual(status['bootstrap_sha256'],hashlib.sha256(self.loader).hexdigest())
        self.assertEqual(self.lib.boot(self.flash),0)
    def test_success_and_confirmation(self):
        original=self.snapshot(TRAILER,0x100000)
        self.assertTrue(self.transfer())
        self.assertEqual(self.lib.boot(self.flash),1)
        self.assertEqual(self.call('/confirm',dict(device_uid=UID,version='1.2.6'))[0],409)
        self.assertEqual(self.call('/confirm',dict(device_uid=UID,version=VERSION))[0],200)
        self.assertEqual(self.lib.boot(self.flash),1)
        self.assertEqual(self.snapshot(TRAILER,0x100000),original)
    def test_identity_and_duplicate_chunks(self):
        self.assertEqual(self.call('',auth=0)[0],401)
        self.assertEqual(self.call('/enter',dict(device_uid=UID),idle=0)[0],409)
        self.assertEqual(self.call('/enter',dict(device_uid='0000'))[0],409)
        self.assertEqual(self.begin('1.2.6')[0],409)
        _,tr=self.begin()
        self.assertEqual(self.begin()[1],tr)
        payload=dict(session=tr['session'],offset=0,data=self.image[:1024].hex())
        self.assertEqual(self.call('/chunk',dict(payload,offset=4))[0],409)
        self.assertEqual(self.call('/chunk',payload)[1]['offset'],1024)
        self.assertEqual(self.call('/chunk',payload)[1]['offset'],1024)
        self.assertEqual(self.call('/chunk',dict(payload,data='00'*1024))[0],409)
        self.assertEqual(self.call('/finish',dict(session=tr['session']))[0],409)
    def test_wrong_bank_model_version_padding_and_hash(self):
        valid=self.image
        for bad in (app(0,VERSION),app(1,'3.4.5'), b'X'*len(valid),
                    valid[:-1]+bytes([valid[-1]^1])):
            self.image=bad
            self.assertFalse(self.transfer())
            self.assertEqual(self.lib.boot(self.flash),0)
        self.image=valid
        _,tr=self.begin()
        wrong=self.image[:-1]+bytes([self.image[-1]^1])
        for off in range(0,len(wrong),1024):
            self.call('/chunk',dict(session=tr['session'],offset=off,data=wrong[off:off+1024].hex()))
        self.assertEqual(self.call('/finish',dict(session=tr['session']))[0],409)

    def test_power_loss_at_each_erase_and_program(self):
        baseline=self.snapshot(0,0x200000)
        self.lib.fault(self.flash,-1,0)
        self.assertTrue(self.transfer())
        operations=self.lib.writes(self.flash)
        for until in (0,1,3,4,19,24,128,255,1024,4096):
            for op in range(1,operations+1):
                self.seed(0,baseline);self.lib.fault(self.flash,-1,0)
                self.assertEqual(self.lib.boot(self.flash),0)
                self.lib.fault(self.flash,op,until)
                self.transfer();self.lib.fault(self.flash,-1,0)
                bank=self.lib.boot(self.flash)
                self.assertIn(bank,(0,1),(op,until))
                if bank==1:self.assertEqual(self.snapshot(BANKS[1],len(self.image)),self.image)
                self.assertEqual(self.lib.boot(self.flash),0)
            self.assertEqual(self.snapshot(TRAILER,0x100000),baseline[TRAILER:])
    def test_torn_trial_and_confirmation_transitions(self):
        self.assertTrue(self.transfer())
        pending=self.snapshot(0,0x200000)
        for prefix in range(5):
            self.seed(0,pending);self.lib.fault(self.flash,1,prefix)
            self.lib.boot(self.flash);self.lib.fault(self.flash,-1,0)
            bank=self.lib.boot(self.flash)
            self.assertIn(bank,(0,1))
            if bank==1:self.assertEqual(self.lib.boot(self.flash),0)
            self.seed(0,pending);self.assertEqual(self.lib.boot(self.flash),1)
            self.lib.fault(self.flash,1,prefix);self.call('/confirm',dict(device_uid=UID,version=VERSION))
            self.lib.fault(self.flash,-1,0)
            self.assertIn(self.lib.boot(self.flash),(0,1))
    def test_both_linked_application_images(self):
        if not self.build:self.skipTest('Set PICOFERM_AB_BUILD to the native cross-link output')
        # Put a known-good older synthetic application in each bank in turn,
        # then stream the real opposite-slot binary through the actual HTTP receiver.
        for target in (1,0):
            active=1-target
            initial=app(active,'1.2.6')
            self.seed(4096,b'\xff'*8192);self.seed(BANKS[active],initial)
            self.seed((1+active)*4096,bank_record(active,initial))
            self.assertEqual(self.lib.boot(self.flash),active)
            self.image=(self.build/f'bank-{target}.bin').read_bytes()
            self.assertTrue(self.transfer());self.assertEqual(self.lib.boot(self.flash),target)
            self.assertEqual(self.call('/confirm',dict(device_uid=UID,version=VERSION))[0],200)
            self.assertEqual(self.lib.boot(self.flash),target)

if __name__=='__main__':unittest.main(verbosity=2)
