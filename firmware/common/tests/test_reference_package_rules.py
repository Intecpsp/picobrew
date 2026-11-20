"""Reject dangerous address changes even when general bounds still fit."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import package_reference as package

class PackageRulesTests(unittest.TestCase):
    def test_reconstructed_sources_and_headers_cannot_hide_behind_a_report_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);elf=root/'application.elf';report=root/'build.json'
            elf.write_bytes(b'identity fixture')
            core=root/'firmware/z-series/reverse-engineered/src/core.cpp'
            header=root/'firmware/z-series/reverse-engineered/include/core.hpp'
            builder=root/'firmware/z-series/modified/tools/build_firmware.py'
            for path in (core,header,builder):path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'original')
            evidence=dict(complete_application_linked=True,device_model='z-series',elf_sha256=package.digest(elf),
                build_script_sha256=package.digest(builder),reconstructed_object_report_sha256='old-report-hash',
                reconstructed_sources=[dict(source=str(core.relative_to(root)),source_sha256=package.digest(core))],
                reconstructed_headers={str(header.relative_to(root)):package.digest(header)})
            report.write_text(json.dumps(evidence))
            self.assertEqual(package.verify_report('z-series',elf,report,root,None,None)[1],3)
            for path in (core,header):
                path.write_bytes(b'changed after ELF link')
                with self.assertRaisesRegex(ValueError,'Build input changed'):
                    package.verify_report('z-series',elf,report,root,None,None)
                path.write_bytes(b'original')
            del evidence['reconstructed_sources'];report.write_text(json.dumps(evidence))
            with self.assertRaisesRegex(ValueError,'embed reconstructed'):
                package.verify_report('z-series',elf,report,root,None,None)

    def test_build_report_requires_the_exact_appliance(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);elf=root/'application.elf';report=root/'build.json'
            elf.write_bytes(b'identity fixture')
            base=dict(complete_application_linked=True,elf_sha256=package.digest(elf))
            # Z and Still share an architecture and layout. A same-architecture
            # ELF hash cannot substitute for the explicitly selected appliance.
            for identity in ({},{'model':'picostill'},{'device_model':'picostill'},
                             {'model':'z-series','device_model':'picostill'}):
                report.write_text(json.dumps(dict(base,**identity)))
                with self.assertRaisesRegex(ValueError,'model'):
                    package.verify_report('z-series',elf,report,root,None,None)

    def test_component_model_boundaries(self):
        ferm=[dict(file='application.bin',offset=0,bytes=400000,maximum_bytes=0x100000),dict(file='spiffs.bin',offset=0x100000,bytes=0xfb000,maximum_bytes=0xfb000)]
        package.check_components(ferm,0x200000,'picoferm')
        for field,value in (('offset',0x1000),('maximum_bytes',0x101000),('bytes',0x100001)):
            broken=copy.deepcopy(ferm);broken[0][field]=value
            with self.assertRaises(ValueError):package.check_components(broken,0x200000,'picoferm')
        with self.assertRaises(ValueError):package.check_components(ferm,0x400000,'picostill')
        with self.assertRaises(ValueError):package.check_components(ferm[:1],0x200000,'picoferm')

    def test_geometry_is_checked_separately_from_md5(self):
        # A table generator can produce a perfectly valid checksum for the
        # wrong layout; the independent decoder must enforce the chosen model.
        body=b''.join(struct.pack('<HBBII16sI',0x50aa,t,s,o,z,n.encode().ljust(16,b'\0'),0) for n,t,s,o,z in package.PARTITIONS)
        def table(data):return data+b'\xeb\xeb'+b'\xff'*14+hashlib.md5(data).digest()+b'\xff'*(0xc00-len(data)-32)
        self.assertEqual(package.verify_partition_table(table(body))[2]['offset'],0x10000)
        bad=bytearray(body);struct.pack_into('<I',bad,2*32+8,0x200000)
        with self.assertRaises(ValueError):package.verify_partition_table(table(bytes(bad)))
        bad=bytearray(table(body));bad[32*len(package.PARTITIONS)+16]^=1
        with self.assertRaises(ValueError):package.verify_partition_table(bytes(bad))

if __name__=='__main__':unittest.main()
