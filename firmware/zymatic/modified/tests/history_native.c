/* Byte-level EEPROM model, including abrupt loss before every byte commit. */
#include "zymatic_history.h"
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>

static uint8_t eeprom[4096];
_Alignas(uint32_t) uint8_t zh_test_sram[0x4100];
static uint32_t now;
static unsigned mode, writes, cut, busy;
static jmp_buf power_out;
enum { SHORT_RUN, LONG_RUN, CANCEL, FAST_EXIT, POWER_LOSS };
uint8_t zh_test_ready(void) { return !busy; }
uint8_t zh_test_read(uint16_t addr)
{
    assert(!busy && addr < sizeof(eeprom));
    return eeprom[addr];
}
uint8_t zh_test_write(uint16_t addr, uint8_t value)
{
    assert(addr < sizeof(eeprom));
    if (busy)
        return 0;
    if (eeprom[addr] == value)
        return 1;
    if (++writes == cut)
        longjmp(power_out, 1);
    eeprom[addr] = value;
    return 1;
}
uint32_t zh_test_millis(void) { return now; }
void zh_test_cleanup(void)
{
    memset(zh_test_sram + 0x23c0, 0, 2);
    zh_test_sram[0x2288] = 0;
}
static void drain(void)
{
    for (unsigned i = 0; i < 160; ++i)
        zymatic_history_poll(now);
}
void zh_test_program(uint8_t index)
{
    assert(index == 0);
    zh_test_sram[0x23c0] = 0;
    if (mode == FAST_EXIT) {
        zymatic_program_cleanup();
        return;
    }
    drain();
    for (unsigned i = 0; i < (mode == LONG_RUN ? 230u : 2u); ++i) {
        now += ZH_INTERVAL;
        drain();
    }
    zh_test_sram[0x23c0] = 1;
    now += 1500;
    drain();
    if (mode == POWER_LOSS)
        longjmp(power_out, 1);
    if (mode == CANCEL)
        zh_test_sram[0x2288] = 1;
    now += 1000;
    zymatic_program_cleanup();
}
static void prepare(void)
{
    memset(eeprom, 0xff, sizeof(eeprom));
    memset(zh_test_sram, 0, sizeof(zh_test_sram));
    memcpy(zh_test_sram + 0x16be, "Pilot \"IPA\"", 12);
    zh_test_sram[0x16be + 21] = 2;
    zh_test_sram[0x16be + 24] = 153;
    zh_test_sram[0x16be + 26] = 1;
    zh_test_sram[0x16be + 30] = 204;
    zh_test_sram[0x16be + 32] = 2;
    const float w = 152.375f, h = 170.5f;
    memcpy(zh_test_sram + 0x23dc, &w, 4);
    memcpy(zh_test_sram + 0x23e0, &h, 4);
    now = UINT32_MAX - 30000;
    writes = cut = mode = busy = 0;
    zymatic_history_init();
}
static void run(unsigned kind)
{
    mode = kind;
    zymatic_run_program(0);
    drain();
}
static void verify(uint32_t id, unsigned minimum)
{
    ZymaticHistorySession s;
    assert(zymatic_history_session(id, &s));
    assert(s.id == id && strcmp(s.name, "Pilot \"IPA\"") == 0 && s.count >= minimum);
    uint16_t elapsed = 0;
    for (unsigned i = 0; i < s.count; ++i) {
        ZymaticHistoryRow r;
        assert(zymatic_history_row(id, i, &r) && r.wort == 1524 && r.heat == 1705);
        if (i)
            assert(elapsed < r.seconds);
        elapsed = r.seconds;
        if (r.step == 0)
            assert(r.target == 153 && r.location == 1);
        else if (r.step == 1)
            assert(r.target == 204 && r.location == 2);
        else
            assert(r.step == 255 && r.target == 255 && r.location == 255);
    }
    assert(s.duration == elapsed);
    ZymaticHistoryRow r;
    assert(!zymatic_history_row(id, s.count, &r));
}
void zh_test_seed(void)
{
    prepare();
    run(SHORT_RUN);
}
void zh_test_history_suite(void)
{
    ZymaticHistorySession s;
    prepare();
    eeprom[0x180] = 0x2d;
    zymatic_history_init();
    run(SHORT_RUN);
    assert(!zymatic_history_ready() && !writes && eeprom[0x180] == 0x2d);

    prepare();
    run(SHORT_RUN);
    verify(1, 5);
    assert(zymatic_history_session(1, &s) && s.flags == ZH_COMPLETE);
    busy = 1;
    assert(!zymatic_history_ready() && !zymatic_history_session(1, &s));
    busy = 0;
    zymatic_history_init();
    verify(1, 5);
    run(CANCEL);
    assert(zymatic_history_session(2, &s) && s.flags == ZH_ABORTED);
    mode = POWER_LOSS;
    if (!setjmp(power_out)) {
        zymatic_run_program(0);
        assert(0);
    }
    zymatic_history_init();
    verify(3, 4);
    assert(zymatic_history_session(3, &s) && s.flags == 0);
    for (unsigned i = 0; i < 8; ++i)
        run(SHORT_RUN);
    assert(!zymatic_history_session(3, &s));
    for (uint32_t i = 4; i <= 11; ++i)
        verify(i, 5);

    run(LONG_RUN);
    verify(12, ZH_ROWS);
    assert(zymatic_history_session(12, &s) && s.flags == (ZH_TRUNCATED | ZH_COMPLETE));
    assert(!zymatic_history_session(11, &s));
    run(SHORT_RUN);
    assert(!zymatic_history_session(12, &s));
    verify(13, 5);
    for (unsigned i = 0; i < sizeof(eeprom); ++i)
        zymatic_factory_clear_byte(0, i, 0);
    assert(!zymatic_history_session(13, &s) && zymatic_history_latest() == 13);
    for (unsigned i = 0x110; i < sizeof(eeprom); ++i)
        assert(eeprom[i] == 0);
    run(SHORT_RUN);
    verify(14, 5);
    for (unsigned i = 0; i < 0x100; ++i)
        assert(eeprom[i] == 0);
    for (unsigned i = 0xff0; i < 4096; ++i)
        assert(eeprom[i] == 0);

    prepare();
    mode = FAST_EXIT;
    zymatic_run_program(0);
    zymatic_run_program(0);
    drain();
    drain();
    assert(zymatic_history_latest() == 2 && zymatic_history_session(1, &s) && s.flags == ZH_COMPLETE);
    assert(zymatic_history_session(2, &s) && s.flags == ZH_COMPLETE);

    prepare();
    run(SHORT_RUN);
    eeprom[0x210 + 7] ^= 0x80; /* committed wort sample with an invalid CRC */
    zymatic_history_init();
    assert(!zymatic_history_ready());

    /* Reboot at every possible byte boundary on a blank unit, and at every
     * boundary replacing the header of a slot that already contains a brew. */
    for (unsigned existing = 0; existing <= 8; existing += 8) {
        prepare();
        for (unsigned i = 0; i < existing; ++i)
            run(SHORT_RUN);
        uint8_t saved[4096];
        memcpy(saved, eeprom, sizeof(saved));
        writes = 0;
        run(SHORT_RUN);
        unsigned limit = writes;
        for (unsigned loss = 1; loss <= limit; ++loss) {
            memcpy(eeprom, saved, sizeof(saved));
            zymatic_history_init();
            writes = 0;
            cut = loss;
            if (!setjmp(power_out)) {
                run(SHORT_RUN);
                assert(0);
            }
            cut = 0;
            zymatic_history_init();
            assert(zymatic_history_ready());
            uint32_t latest = zymatic_history_latest();
            assert(latest == existing || latest == existing + 1);
            if (zymatic_history_session(existing + 1, &s))
                verify(existing + 1, 0);
            run(SHORT_RUN);
            verify(latest + 1, 5);
        }
    }
}
