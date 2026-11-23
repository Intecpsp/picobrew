#ifndef ZYMATIC_HISTORY_H
#define ZYMATIC_HISTORY_H
#include <stdint.h>

/* Each row represents a measured second, the original Fahrenheit temperature
 * channels in tenths of a degree, and the currently addressed stock recipe step.
 * 0xffff / 0xff represent unavailable measurements. */
enum { ZH_SLOTS = 8, ZH_ROWS = 222, ZH_INTERVAL = 60000UL };
typedef struct {
    uint16_t seconds, wort, heat;
    uint8_t target, location, step;
} ZymaticHistoryRow;
typedef struct {
    uint32_t id;
    uint16_t first_elapsed, duration;
    uint8_t count, flags;
    char name[21];
} ZymaticHistorySession;
enum { ZH_ACTIVE = 1, ZH_COMPLETE = 2, ZH_TRUNCATED = 4, ZH_ABORTED = 8 };

uint16_t zymatic_temperature_tenths(uint32_t bits);
void zymatic_history_init(void);
void zymatic_history_poll(uint32_t now);
void zymatic_program_cleanup(void);
void zymatic_run_program(uint8_t index);
void zymatic_factory_clear_byte(void *object, uint16_t address, uint8_t value);
uint32_t zymatic_history_latest(void);
uint8_t zymatic_history_ready(void);
uint8_t zymatic_history_session(uint32_t id, ZymaticHistorySession *out);
uint8_t zymatic_history_row(uint32_t id, uint16_t offset, ZymaticHistoryRow *out);
#endif
