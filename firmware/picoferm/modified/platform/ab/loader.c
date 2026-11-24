// Independent ROM-SPI bootstrap. OTA writes never include the first flash sector.
#include "boot.h"
#include <stddef.h>
extern int SPIRead(uint32_t, void *, size_t);
extern int SPIWrite(uint32_t, void *, size_t);
extern void ets_wdt_disable(void);
extern void ets_wdt_enable(void);

static int read_rom(void *ctx, uint32_t address, void *dest, uint32_t length) {
    (void)ctx;
    return SPIRead(address, dest, length);
}
static int write_rom(void *ctx, uint32_t address, const void *data, uint32_t length) {
    (void)ctx;
    return SPIWrite(address, (void *)data, length);
}
void main(void) {
    const FermBootIO io = {0, read_rom, write_rom};
    uint32_t pc;
    FermBootRecord record;
    ets_wdt_disable();
    int bank = ferm_select(&io);
    if (bank >= 0 && ferm_read_record(&io, (unsigned)bank, &record) && ferm_image(&io, &record, 1, &pc)) {
        ets_wdt_enable();
        __asm__ volatile("movi a1, 0x3ffffff0\n\tjx %0" :: "a"(pc) : "memory");
    }
    // Avoid running a partial image. The hardware UART ROM loader remains available.
    ets_wdt_enable();
    for (;;) {}
}
