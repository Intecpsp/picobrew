// PicoFerm two-bank ESP8266 layout. All flash addresses are physical byte offsets.
#ifndef PICOFIRM_AB_BOOT_H
#define PICOFIRM_AB_BOOT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FERM_SECTOR 4096u
#define FERM_BANK_SIZE 0x7d000u
#define FERM_PENDING 0xfffffffeu
#define FERM_TRIED 0xfffffffcu
#define FERM_CONFIRMED 0xfffffff8u
#define FERM_RECORD_MAGIC 0x31424650u /* PFB1 */
#define FERM_RECORD_ADDRESS(bank) ((1u + (bank)) * FERM_SECTOR)
#define FERM_BANK_ADDRESS(bank) (0x3000u + (bank) * 0x80000u)
typedef struct {
    uint32_t magic, bank, generation, size, image_crc, header_crc, state;
} FermBootRecord;
typedef struct {
    void *context;
    int (*read)(void *, uint32_t address, void *data, uint32_t length);
    int (*write)(void *, uint32_t address, const void *data, uint32_t length);
} FermBootIO;
uint32_t ferm_crc32(uint32_t crc, const void *data, uint32_t length);
// Record validity includes a complete read and CRC of the declared image.
// load additionally copies each validated RAM segment, then returns its entry.
int ferm_image(const FermBootIO *io, const FermBootRecord *record, int load, uint32_t *entry);
int ferm_read_record(const FermBootIO *io, unsigned bank, FermBootRecord *record);
int ferm_transition(const FermBootIO *io, unsigned bank, uint32_t prior, uint32_t state);
int ferm_select(const FermBootIO *io);
#ifdef __cplusplus
}
#endif
#endif
