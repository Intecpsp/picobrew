"""Compare the standalone renderer to every original packed bitmap, pixel for pixel."""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from native_host import compiler, executable_name, runtime_environment

source = (ROOT / 'src/ui/assets.c').read_text()
aliases = re.findall(r'^#define (pico_ui_\w+) \(pico_packed_pixels \+ \d+\)$',
                     (ROOT / 'src/ui/packed_bitmaps.h').read_text(), re.M)
with tempfile.TemporaryDirectory() as directory:
    tests = []
    declarations = []
    for name in aliases:
        match = re.search(r'const uint8_t ' + name + r'\[\d+\] = (\{[^}]+\});', source)
        declarations.append('static const uint8_t raw_' + name + '[] = ' + match[1] + ';')
        size = len(re.findall(r'0x[0-9a-f]+', match[1]))
        width = 40 if name.endswith('device') else 80 if name.endswith('logo') else 128
        tests.append(f'check({name},raw_{name},{width},{size*8//width});')
    harness = Path(directory)/'check.c'
    harness.write_text('#include "ui_internal.h"\n#include <assert.h>\n' +
        '\n'.join(declarations) + """
PicoUi *pico_ui_active;
static void check(const uint8_t *packed, const uint8_t *raw, int width, int height)
{
    PicoDisplay a = {0}, b = {0};
    a.width = b.width = 128; a.height = b.height = 64;
    pico_graphics_bitmap(&a, 0, 0, packed, width, height, 1);
    pico_graphics_bitmap(&b, 0, 0, raw, width, height, 1);
    assert(memcmp(a.framebuffer, b.framebuffer, sizeof a.framebuffer) == 0);
}
int main(void) {
""" + '\n'.join(tests) + '\nreturn 0; }\n')
    program = Path(directory)/executable_name('bitmap-check')
    subprocess.run([compiler(), '-std=c11','-DPICO_STANDALONE=1','-I'+str(ROOT/'include'),
                    '-I'+str(ROOT/'src/ui'),str(harness),str(ROOT/'src/ui/assets.c'),
                    str(ROOT/'src/ui/graphics.c'),'-o',str(program)], check=True)
    subprocess.run([str(program)],check=True,env=runtime_environment())
print(f'{len(aliases)} standalone bitmaps match the original OLED pixels')
