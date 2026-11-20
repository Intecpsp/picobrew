"""Owned front-panel lifecycle across actual mailboxes, editors and painters."""
from pathlib import Path
import json
import hashlib
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class HeadApplicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary=tempfile.TemporaryDirectory(prefix='head-application-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.binary=Path(cls.temporary.name)/'head-application'
        sources=sorted((ROOT/'src').glob('head_*.cpp'))+[ROOT/'src'/name for name in ['display.cpp','display_text.cpp','display_fonts.cpp','display_effects.cpp','ui_catalog.cpp','ui_screen_tick.cpp','quantity.cpp','enums.cpp']]+[ROOT/'tests/head_application_probe.cpp']
        subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++11','-O2','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT/'include')]+[str(p)for p in sources]+['-o',str(cls.binary)],check=True)
    def run_scenario(self,value):
        subprocess.run([str(self.binary),str(value)],check=True,capture_output=True,text=True)
    def test_setup_shared_mailbox_priority_and_drop_new(self):self.run_scenario(0)
    def test_encoder_numeric_character_and_wifi_round_trips(self):self.run_scenario(1)
    def test_persisted_settings_and_original_display_fields(self):self.run_scenario(2)
    def test_owned_remote_recipes_and_returning_diagnostics(self):self.run_scenario(3)
    def test_console_and_actual_periodic_painter_owners(self):self.run_scenario(4)

if __name__=='__main__':unittest.main()
