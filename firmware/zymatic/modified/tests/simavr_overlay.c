/* Instruction-level integration against simavr's ATmega1284P, with a simulated
 * W5100 on its SPI bus. No USB or physical peripheral access. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sim_avr.h>
#include <sim_io.h>
#include <avr_spi.h>
#include <avr_eeprom.h>

typedef struct {
    avr_t *avr;
    avr_irq_t *input;
    uint8_t regs[0x8000], opcode, part;
    uint16_t addr, received, tx_len;
    unsigned writes, transactions;
} Socket;

static void run_until(avr_t *avr, uint32_t target, uint32_t budget)
{
    uint64_t until = avr->cycle + budget;
    while (avr->pc != target && avr->cycle < until && avr->state != cpu_Crashed)
        avr_run(avr);
    if (avr->pc != target) {
        fprintf(stderr, "CPU stopped at %05x; expected %05x\n", avr->pc, target);
        exit(1);
    }
}

static void spi_byte(avr_irq_t *irq, uint32_t value, void *param)
{
    (void)irq;
    Socket *s = param;
    uint8_t byte = (uint8_t)value, answer = 0;
    if (s->part == 0) {
        ++s->transactions;
        assert(byte == 0x0f || byte == 0xf0);
        s->opcode = byte;
    } else if (s->part == 1) {
        s->addr = (uint16_t)byte << 8;
    } else if (s->part == 2) {
        s->addr |= byte;
    } else {
        assert(s->addr < sizeof(s->regs));
        if (s->opcode == 0x0f) {
            answer = s->regs[s->addr];
        } else {
            ++s->writes;
            assert((s->addr >= 0x700 && s->addr < 0x730) || (s->addr >= 0x5800 && s->addr < 0x6000));
            if (s->addr == 0x702) s->regs[s->addr] &= (uint8_t)~byte;
            else s->regs[s->addr] = byte;
            if (s->addr == 0x701) {
                if (byte == 1) s->regs[0x703] = 0x13;
                else if (byte == 2) s->regs[0x703] = 0x14;
                else if (byte == 0x40) {
                    uint16_t next = ((uint16_t)s->regs[0x728] << 8) | s->regs[0x729];
                    assert(next <= s->received);
                    uint16_t left = s->received - next;
                    s->regs[0x726] = left >> 8;
                    s->regs[0x727] = (uint8_t)left;
                } else if (byte == 0x20) {
                    s->tx_len = (uint16_t)s->regs[0x724] << 8 | s->regs[0x725];
                    s->regs[0x702] |= 16;
                } else if (byte == 8 || byte == 16) s->regs[0x703] = 0;
                else assert(!"unexpected W5100 command");
                s->regs[0x701] = 0;
            }
        }
    }
    s->part = (s->part + 1) % 4;
    avr_raise_irq(s->input, answer);
}

static uint64_t max_cycles;

enum { OUTER, MENU, PROGRAM };
static const uint32_t call_site[] = {0x1a7d4, 0x831a, 0xf484};
static const uint32_t continuation[] = {0x1a7cc, 0x8322, 0xf48c};

static void foreground(avr_t *avr, int site)
{
    /* Enter through the original foreground CALL. Setup is left at the OEM
     * entry since no physical switches, heaters or sensor buses are modeled. */
    /* Advance the OEM millisecond timer explicitly between foreground turns. */
    uint32_t now;
    memcpy(&now, avr->data + 0x2b25, sizeof(now));
    now += 24;
    memcpy(avr->data + 0x2b25, &now, sizeof(now));
    avr->pc = call_site[site];
    uint8_t registers[16];
    memcpy(registers, avr->data + 2, sizeof(registers));
    uint64_t start = avr->cycle;
    uint8_t rampz = avr->data[0x5b];
    uint16_t sp = avr->data[0x5d] | avr->data[0x5e] << 8;
    run_until(avr, continuation[site], 400000);
    assert(!memcmp(registers, avr->data + 2, sizeof(registers)));
    uint64_t cycles = avr->cycle - start;
    if (cycles > max_cycles) max_cycles = cycles;
    assert(sp == (avr->data[0x5d] | avr->data[0x5e] << 8));
    assert(avr->data[0x5b] == rampz);
}

int main(int argc, char **argv)
{
    assert(argc == 8);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    uint8_t image[0x1e000];
    size_t size = fread(image, 1, sizeof(image), file);
    assert(size > 0x1bc00 && fgetc(file) == EOF);
    fclose(file);
    avr_t *avr = avr_make_mcu_by_name("atmega1284p");
    assert(avr && avr_init(avr) == 0);
    avr->frequency = 16000000;
    avr_loadcode(avr, image, (uint32_t)size, 0);
    memset(avr->data + 0x100, 0xa5, 0x4000);
    if (!strcmp(argv[6], "corrupt")) {
        /* Flip an initialized, non-code byte after composing the private image. */
        avr->flash[size - 5] ^= 1;
        run_until(avr, strtoul(argv[5], NULL, 0), 60000000);
        puts("Damaged private image halted before OEM setup.");
        return 0;
    }
    run_until(avr, 0x11168, 60000000); /* reset, boot, constructors and actual custom setup */
    uint32_t hostname = strtoul(argv[2], NULL, 0), bss_end = strtoul(argv[3], NULL, 0);
    assert(!strcmp((const char *)avr->data + hostname, "brewery.lan"));
    assert(!strcmp((const char *)avr->data + strtoul(argv[4], NULL, 0), "brewery.lan:8080"));
    uint16_t sum = 0;
    for (unsigned i = 0; i < 65536; i += 4) sum += (uint16_t)(2 * (image[i] | image[i+1] << 8));
    for (int i = 0; i < 3; ++i) {
        uint16_t stamp = i == 1 ? 0x4d17 : 0x4d16;
        uint16_t observed = i == 2 ? sum ^ 1 : sum;
        avr->data[14] = (uint8_t)stamp; avr->data[15] = stamp >> 8;
        avr->data[22] = (uint8_t)observed; avr->data[23] = observed >> 8;
        avr->pc = 0x7adc;
        run_until(avr, i == 0 ? 0x7ae2 : 0x7ae6, 2000);
    }
    assert((avr->data[0x9c6] | avr->data[0x9c7] << 8) == (int)bss_end);
    assert(avr->data[bss_end] == 0xa5);
    if (!strncmp(argv[6], "factory", 7)) {
        /* Enter the stock commissioner's checksum write. Keep this simulated
         * board's EEPROM when switching the AVR back to its original image. */
        avr->pc = 0x106fc;
        if (!strcmp(argv[6], "factory-bad")) {
            avr->flash[0x100] ^= 1;
            run_until(avr, strtoul(argv[5], NULL, 0), 3000000);
            puts("Damaged low bank halted the commissioning writer before the stamp.");
        } else {
            run_until(avr, 0x10700, 3000000);
            uint8_t bytes[2] = {0};
            avr_eeprom_desc_t descriptor = {.ee=bytes,.offset=0x0ffa,.size=sizeof(bytes)};
            assert(avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &descriptor) == 0);
            assert(bytes[0] == 0x16 && bytes[1] == 0x4d);
            FILE *original = fopen(argv[7], "rb");
            assert(original && fread(avr->flash, 1, 113610, original) == 113610 && fgetc(original) == EOF);
            fclose(original);
            avr->pc = 0x7a74;
            run_until(avr, 0x7ae2, 3000000);
            puts("Stock 1.1.14 accepted the EEPROM stamp from custom commissioning after restoration.");
        }
        avr_terminate(avr);
        return 0;
    }
    static Socket socket;
    socket.avr = avr;
    avr_irq_t *out = avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_OUTPUT);
    socket.input = avr_io_getirq(avr, AVR_IOCTL_SPI_GETIRQ(0), SPI_IRQ_INPUT);
    assert(out && socket.input);
    avr_irq_register_notify(out, spi_byte, &socket);
    /* Start at an actual Ethernet serial-reader call. Model only the external
     * device read. Run the stock formatting code and validated capture wrapper. */
    avr->pc = 0xb9c8;
    avr->data[24] = 0x00;
    avr->data[25] = 0x32;
    run_until(avr, 0x4120, 10000);
    uint16_t rom = avr->data[24] | avr->data[25] << 8;
    const uint8_t test_rom[] = {1,0x11,0x22,0x33,0x44,0x55,0x66,0x77};
    memcpy(avr->data + rom, test_rom, sizeof(test_rom));
    avr->data[24] = 1;
    avr->pc = 0x19dae; /* RET from the simulated one-wire read */
    run_until(avr, 0xb9cc, 100000);
    assert(avr->data[24] == 1 && strlen((const char *)avr->data + 0x3200) == 12);
    const float wort = 151.25f, heater = 181.5f;
    memcpy(avr->data + 0x23dc, &wort, sizeof(wort));
    memcpy(avr->data + 0x23e0, &heater, sizeof(heater));
    int site = strncmp(argv[6], "program-", 8) == 0 ? PROGRAM : MENU;
    foreground(avr, OUTER);
    assert(!socket.writes);
    avr->data[0x249f] = 0;  /* pending press: same original return 0xff, then 0 */
    foreground(avr, site);
    assert(avr->data[24] == 0xff && avr->data[0x249f] == 1);
    foreground(avr, site);
    assert(avr->data[24] == 0);
    /* Model an already initialized SPI bus and the DHCP success state used by
     * OEM Ethernet. Wi-Fi selection above performed no SPI transactions. */
    avr_core_watch_write(avr, 0x2a, avr->data[0x2a] | 0xc0);
    avr_core_watch_write(avr, 0x2b, (avr->data[0x2b] & 0x3f) | 0x40);
    /* Slowest admitted hardware SPI setting, F_CPU/8. Use a dense framebuffer. */
    avr_core_watch_write(avr, 0x4c, 0x51);
    avr_core_watch_write(avr, 0x4d, 0x01);
    for (unsigned i = 0; i < 1024; ++i) avr->data[0x266a+i] = (uint8_t)(255 - i);
    avr->data[0x230b] = 1;
    avr->data[0x249f] = 0;
    unsigned before_press = socket.transactions;
    foreground(avr, site);
    assert(avr->data[24] == 0xff && socket.transactions == before_press);
    socket.regs[0x1a] = socket.regs[0x1b] = 0x55;
    socket.regs[0x720] = 8;
    if (site == PROGRAM) {
        unsigned before = socket.transactions;
        avr->data[0x2334] = 1;
        foreground(avr, site);
        assert(socket.transactions == before);
        avr->data[0x2334] = 0;
        uint16_t brk = 0x3ffe;
        memcpy(avr->data + 0x2c84, &brk, sizeof(brk));
        foreground(avr, site);
        assert(socket.transactions == before);
        avr->data[0x2c84] = avr->data[0x2c85] = 0;
    }
    for (int i = 0; socket.regs[0x703] != 0x14 && i < 20; ++i) foreground(avr, site);
    assert(socket.regs[0x703] == 0x14 && socket.regs[0x704] == 0 && socket.regs[0x705] == 80);
    socket.regs[0x703] = 0x17;
    uint8_t display = strstr(argv[6], "display") != NULL;
    const char *path = display ? "/api/v1/display" : site == PROGRAM ? "/api/v1/status" : "/api/v1";
    const char *fixture_key = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
    socket.received = (uint16_t)snprintf((char *)&socket.regs[0x7800], 512,
        "GET %s HTTP/1.1\r\nHost: brewery\r\nAuthorization: Bearer %s\r\n\r\n", path, fixture_key);
    socket.regs[0x726] = socket.received >> 8;
    socket.regs[0x727] = (uint8_t)socket.received;
    for (int i = 0; !socket.tx_len && i < 140; ++i) foreground(avr, site);
    if (!socket.tx_len) fprintf(stderr, "No response: socket status %02x, RX pending %u, traffic %u writes, max %llu cycles\n",
        socket.regs[0x703], socket.regs[0x727], socket.writes, (unsigned long long)max_cycles);
    assert(socket.tx_len && socket.tx_len < 2048);
    socket.regs[0x5800 + socket.tx_len] = 0;
    const char *reply = (const char *)&socket.regs[0x5800];
    const char *expected = display && site == PROGRAM ? "503" : "200 OK";
    const char *needle = display ? (site == PROGRAM ? "display_busy" : "revision") :
                                   (site == PROGRAM ? "program_running" : "1.2.0");
    if (!strstr(reply, expected) || !strstr(reply, needle)) {
        fprintf(stderr, "Unexpected HTTP response: %s\n", reply);
        return 1;
    }
    if (site == PROGRAM && !display) {
        assert(strstr(reply, (const char *)avr->data + 0x3200));
        assert(strstr(reply, "\"wort_f\":151.3") && strstr(reply, "\"thermoblock_f\":181.5"));
        assert(strstr(reply, "\"busy\":true"));
    }
    /* At the slowest accepted SPI setting this exercised request path must
     * return to the stock program within 4 ms. This is a regression budget
     * for the model, not a physical worst-case response-time measurement. */
    if (site == PROGRAM) assert(max_cycles < 64000);
    printf("Reset to OEM setup; %s served %s, %u bytes, max %llu cycles at 16 MHz.\n",
           site == PROGRAM ? "program button" : "menu", path, socket.tx_len, (unsigned long long)max_cycles);
    avr_terminate(avr);
    return 0;
}
