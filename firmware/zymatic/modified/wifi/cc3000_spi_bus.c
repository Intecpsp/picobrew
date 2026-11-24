/* Bench-only board handoff for the OEM Zymatic 1.1.14 display and radio. */
#include "cc3000_spi_bus.h"
#include <avr/io.h>
#include <util/atomic.h>
#include <stdint.h>

enum { BUS_IDLE, BUS_RADIO, BUS_DISPLAY };
static volatile uint8_t owner;
static volatile uint8_t display_pending;

bool zym_spi_take_radio(void)
{
    bool claimed = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (owner == BUS_IDLE && !display_pending) {
            owner = BUS_RADIO;
            claimed = true;
        }
    }
    return claimed;
}

void zym_spi_release_radio(void)
{
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (owner == BUS_RADIO)
            owner = BUS_IDLE;
    }
}

void zym_spi_flush_pending(void)
{
    bool claimed = false;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        if (owner == BUS_IDLE && display_pending) {
            owner = BUS_DISPLAY;
            display_pending = 0;
            claimed = true;
        }
    }
    if (claimed) {
        /* Both patched paths set mode 0 before flushing this same object.
         * Its 1024-byte framebuffer holds the most recent drawing. */
        SPCR &= (uint8_t)~(_BV(CPOL) | _BV(CPHA));
        void (*flush)(void *) = (void (*)(void *))(0x15e46uL / 2);
        flush((void *)(uintptr_t)0x1693);
        owner = BUS_IDLE;
    }
}

void zym_spi_request_display(void)
{
    display_pending = 1;
    zym_spi_flush_pending();
}
