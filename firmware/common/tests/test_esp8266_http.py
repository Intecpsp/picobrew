"""Exercise the raw ESP8266 listener against pinned SDK declarations."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from esp8266_adapters_native import verify_sdk

ROOT=Path(__file__).resolve().parents[1]

class HttpListenerTests(unittest.TestCase):
    def test_actual_listener_and_http_transport(self):
        source=os.environ.get('ESP8266_ARDUINO')
        if not source:self.skipTest('Set ESP8266_ARDUINO to the pinned Arduino 2.4.1 SDK')
        sdk=Path(source);verify_sdk(sdk)
        includes=['-I'+str(p) for p in (ROOT/'tests/esp8266_sdk_host',ROOT/'include',ROOT/'platform/esp8266',sdk/'cores/esp8266',sdk/'tools/sdk/include',sdk/'tools/sdk/lwip/include')]
        flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fno-pie']
        with tempfile.TemporaryDirectory(prefix='esp8266-http-') as folder:
            output=Path(folder);obj=output/'http.o'
            subprocess.run(['gcc','-std=c11',*flags,*includes,'-c',str(ROOT/'src/http_server.c'),'-o',str(obj)],check=True)
            binary=output/'test'
            subprocess.run(['g++','-std=c++11','-DLWIP_OPEN_SRC',*flags,*includes,str(ROOT/'platform/esp8266/http.cpp'),str(ROOT/'tests/esp8266_http_test.cpp'),str(obj),'-no-pie','-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=30,env=dict(os.environ,ASAN_OPTIONS=os.environ.get('ASAN_OPTIONS','detect_leaks=0'),UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'))

if __name__=='__main__':unittest.main(verbosity=2)
