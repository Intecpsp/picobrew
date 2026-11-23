/* Execute each replaced OEM read on both the stock and composed ATmega1284P.
 * Check all table bytes, the surrounding raw-flash fallback, both input flag
 * patterns and the exact architectural state at the next OEM instruction. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sim_avr.h>

static avr_t *load_image(const char *path)
{
    static uint8_t image[0x20000];
    FILE *file = fopen(path, "rb");
    assert(file);
    size_t size = fread(image, 1, sizeof image, file);
    assert(size && fgetc(file) == EOF);
    fclose(file);
    avr_t *cpu = avr_make_mcu_by_name("atmega1284p");
    assert(cpu && avr_init(cpu) == 0);
    cpu->frequency = 16000000;
    avr_loadcode(cpu, image, size, 0);
    return cpu;
}

static unsigned read_at(avr_t *cpu, uint32_t site, uint16_t address, uint8_t flags)
{
    cpu->pc = site;
    for (unsigned r = 0; r < 32; ++r) cpu->data[r] = (uint8_t)(r * 13 + address);
    cpu->data[30] = (uint8_t)address;
    cpu->data[31] = address >> 8;
    cpu->data[26] = 0;
    cpu->data[27] = 0x3b;
    cpu->data[0x3b00] = 0x73;
    cpu->data[0x5d] = 0xff;
    cpu->data[0x5e] = 0x40;
    cpu->data[0x5b] = 1; /* RAMPZ is caller-owned state. */
    for (unsigned b = 0; b < 8; ++b) cpu->sreg[b] = (flags >> b) & 1;
    const uint64_t before = cpu->cycle;
    while (cpu->pc != site + 4 && cpu->cycle - before < 9000 && cpu->state != cpu_Crashed)
        avr_run(cpu);
    assert(cpu->pc == site + 4 && cpu->data[0x5d] == 0xff && cpu->data[0x5e] == 0x40);
    return (unsigned)(cpu->cycle - before);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    avr_t *stock = load_image(argv[1]), *modified = load_image(argv[2]);
    static const uint32_t sites[] = {0x2662, 0x11cd8, 0x1a870, 0x15954};
    unsigned max = 0, cases = 0;
    for (unsigned s = 0; s < sizeof sites / sizeof *sites; ++s) {
        for (uint16_t a = 0x250; a < 0x1c7b; ++a) {
            for (uint8_t f = 0; f < 2; ++f) {
                const uint8_t flags = f ? 0x75 : 0x0a;
                read_at(stock, sites[s], a, flags);
                unsigned elapsed = read_at(modified, sites[s], a, flags);
                if (elapsed > max) max = elapsed;
                assert(!memcmp(stock->data, modified->data, 32));
                assert(!memcmp(stock->sreg, modified->sreg, 8));
                assert(stock->data[0x5b] == modified->data[0x5b]);
                assert(stock->data[0x3b00] == modified->data[0x3b00]);
                ++cases;
            }
        }
    }
    for (unsigned flags = 0; flags <= 0x7f; flags += 0x7f) {
        for (uint16_t address = 0x2177; address < 0x2277; ++address) {
            read_at(stock, 0x194da, address, flags);
            read_at(modified, 0x194da, address, flags);
            assert(!memcmp(stock->data, modified->data, 32));
            assert(!memcmp(stock->sreg, modified->sreg, 8));
            ++cases;
        }
    }
    printf("%u original/packed read equivalence cases; maximum %u CPU cycles per byte.\n", cases, max);
    avr_terminate(stock);
    avr_terminate(modified);
    return 0;
}
