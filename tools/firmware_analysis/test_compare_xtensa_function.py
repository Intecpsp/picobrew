"""Negative controls for SDK instruction matching; no firmware is executed."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import compare_xtensa_function as match


class Symbol(dict):
    def __init__(self,name,section,value=0,size=0,kind='STT_NOTYPE'):
        super().__init__(st_shndx=section,st_value=value,st_size=size,st_info={'type':kind})
        self.name=name


class Section(dict):
    def __init__(self,name,data=b'',**fields):
        super().__init__(sh_type='SHT_PROGBITS',**fields)
        self.name,self.payload=name,data
    def data(self):return self.payload


class Symbols(Section):
    def __init__(self,values):
        super().__init__('.symtab');self.values=values
    def iter_symbols(self):return iter(self.values)
    def get_symbol(self,index):return self.values[index]


class Relocation(dict):
    def __init__(self,offset,kind,symbol):
        super().__init__(r_offset=offset,r_info_type=kind,r_info_sym=symbol)
        self.entry={'r_addend':0}


class Relocations(Section):
    def __init__(self):
        super().__init__('.rela.text',sh_info=1,sh_link=3)
        self['sh_type']='SHT_RELA'
    def iter_relocations(self):return iter([Relocation(0,20,1),Relocation(3,11,2)])


class Elf:
    little_endian=True
    def __init__(self,_):
        self.sections=[Section(''),Section('.text',bytes.fromhex('0102030405060708')),
                       Section('.literal',bytes.fromhex('78563412')),
                       Symbols([Symbol('checked',1,size=8,kind='STT_FUNC'),
                                Symbol('.literal',2),Symbol('callee','SHN_UNDEF')]),Relocations()]
    def __getitem__(self,key):return 'EM_XTENSA' if key=='e_machine' else None
    def get_section(self,index):return self.sections[index]
    def get_section_by_name(self,name):return next(s for s in self.sections if s.name==name)
    def iter_sections(self):return iter(self.sections)


def instruction(address,mnemonic,operands,data,flows=()):
    return {'address':hex(address),'mnemonic':mnemonic,'operands':operands,
            'bytes':data,'flows':list(flows)}


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        root=Path(self.temp.name)
        self.args=argparse.Namespace(**{name:root/name for name in ('image','manifest','object','reference','original')},
                                    symbol='checked',reference_base=0x1000)
        self.args.object.write_bytes(b'controlled ELF test double')
        self.image=bytearray(64);self.image[:8]=bytes.fromhex('1112131415161718');self.image[32:36]=bytes.fromhex('78563412')
        self.reference=[instruction(0x1000,'l32r',['a2','0x0'],'010203'),
                        instruction(0x1003,'call8',['0x0'],'040506'),instruction(0x1006,'retw.n',[],'0708')]
        self.original=[instruction(0x40000000,'l32r',['a2','0x40000020'],'111213'),
                       instruction(0x40000003,'call8',['0x40000100'],'141516'),
                       instruction(0x40000006,'retw.n',[],'1718')]
    def run_match(self):
        self.args.image.write_bytes(self.image)
        self.args.manifest.write_text(json.dumps({'input':{'sha256':hashlib.sha256(self.image).hexdigest()},
                'layout':{'segments':[{'address':0x40000000,'offset':0,'size':64}]}}))
        self.args.reference.write_text(json.dumps(self.reference));self.args.original.write_text(json.dumps(self.original))
        with patch.object(match,'ELFFile',Elf):return match.compare(self.args)
    def test_exact_literal_and_call_binding(self):
        report=self.run_match()
        self.assertEqual(report['normalized_instruction_count'],3)
        self.assertEqual(report['external_bindings'],{'callee':'0x40000100'})
        self.assertFalse(report['behaviorally_verified'])
    def test_register_change_rejected(self):
        self.original[0]['operands'][0]='a3'
        with self.assertRaisesRegex(ValueError,'structure differs'):self.run_match()
    def test_constant_change_rejected(self):
        self.image[32]^=1
        with self.assertRaisesRegex(ValueError,'literal value differs'):self.run_match()
    def test_source_export_bytes_rejected(self):
        self.reference[0]['bytes']='ff0203'
        with self.assertRaisesRegex(ValueError,'ELF bytes'):self.run_match()
    def test_original_export_bytes_rejected(self):
        self.original[0]['bytes']='ff1213'
        with self.assertRaisesRegex(ValueError,'image bytes'):self.run_match()
    def test_instruction_crossing_function_end_rejected(self):
        self.reference[-1]['bytes']='070800'
        with self.assertRaisesRegex(ValueError,'outside'):self.run_match()
    def test_unmapped_literal_rejected(self):
        self.original[0]['operands'][1]='0x40001000'
        with self.assertRaisesRegex(ValueError,'not present'):self.run_match()
    def test_empty_export_rejected(self):
        self.reference=[]
        with self.assertRaisesRegex(ValueError,'Empty instruction'):self.run_match()
    def test_duplicate_export_rejected(self):
        self.reference.insert(1,self.reference[0].copy())
        with self.assertRaisesRegex(ValueError,'overlapping'):self.run_match()
    def test_call_relaxation_requires_adjacent_matching_register(self):
        direct=[instruction(0x1000,'call8',['0x2000'],'010203')]
        relaxed=[instruction(0x4000,'l32r',['a8','0x5000'],'010203'),
                 instruction(0x4003,'callx8',['a8'],'040506')]
        self.assertEqual(match.shape(match.normalized(direct)),match.shape(match.normalized(relaxed)))
        relaxed[1]['address']='0x4006'
        self.assertNotEqual(match.shape(match.normalized(direct)),match.shape(match.normalized(relaxed)))
        relaxed[1]['address']='0x4003';relaxed[1]['operands']=['a9']
        self.assertNotEqual(match.shape(match.normalized(direct)),match.shape(match.normalized(relaxed)))
    def test_branch_destination_is_preserved(self):
        original=[instruction(0x1000,'beqz',['a2','0x1005'],'010203',['0x1005']),
                  instruction(0x1003,'nop.n',[],'0405'),instruction(0x1005,'retw.n',[],'0607')]
        relocated=[instruction(0x2000,'beqz',['a2','0x2005'],'010203',['0x2005']),
                   instruction(0x2003,'nop.n',[],'0405'),instruction(0x2005,'retw.n',[],'0607')]
        self.assertEqual(match.shape(match.normalized(original)),match.shape(match.normalized(relocated)))
        relocated[0]['operands'][1]='0x2003';relocated[0]['flows']=['0x2003']
        self.assertNotEqual(match.shape(match.normalized(original)),match.shape(match.normalized(relocated)))


if __name__=='__main__':unittest.main()
