#include "boot.h"
#include <stddef.h>

uint32_t ferm_crc32(uint32_t crc, const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    crc = ~crc;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((0u - (crc & 1u)) & 0xedb88320u);
    }
    return ~crc;
}

int ferm_read_record(const FermBootIO *io, unsigned bank, FermBootRecord *r) {
    if (bank > 1 || io->read(io->context, FERM_RECORD_ADDRESS(bank), r, sizeof(*r))) return 0;
    return r->bank == bank && r->magic == FERM_RECORD_MAGIC && r->generation &&
        r->size >= 64 && r->size <= FERM_BANK_SIZE && !(r->size & 15) &&
        (r->state == FERM_PENDING || r->state == FERM_TRIED || r->state == FERM_CONFIRMED) &&
        r->header_crc == ferm_crc32(0, r, offsetof(FermBootRecord, header_crc));
}

int ferm_transition(const FermBootIO *io, unsigned bank, uint32_t prior, uint32_t state) {
    FermBootRecord r;
    if (!ferm_read_record(io, bank, &r) || r.state != prior ||
        !((prior == FERM_PENDING && state == FERM_TRIED) ||
          (prior == FERM_TRIED && state == FERM_CONFIRMED))) return 0;
    uint32_t address = FERM_RECORD_ADDRESS(bank) + offsetof(FermBootRecord, state);
    return !io->write(io->context, address, &state, sizeof(state)) &&
        ferm_read_record(io, bank, &r) && r.state == state;
}

static uint8_t xor_bytes(const void *data, uint32_t length) {
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    while (length--) sum ^= *bytes++;
    return sum;
}

int ferm_image(const FermBootIO *io, const FermBootRecord *r, int load, uint32_t *entry) {
    uint32_t block[64], base = FERM_BANK_ADDRESS(r->bank), crc = 0;
    uint8_t checksum = 0;
    if (r->bank > 1 || r->size < 64 || r->size > FERM_BANK_SIZE || (r->size & 15)) return 0;
    for (uint32_t offset = 0; offset < r->size; offset += sizeof(block)) {
        uint32_t length = r->size - offset;
        if (length > sizeof(block)) length = sizeof(block);
        if (io->read(io->context, base + offset, block, length)) return 0;
        crc = ferm_crc32(crc, block, length);
        checksum ^= xor_bytes(block, length);
    }
    if (crc != r->image_crc || io->read(io->context, base, block, 8) ||
        block[0] != 0x300204e9u) return 0; // DIO, 40 MHz, 2 MiB; exactly four segments.
    checksum ^= xor_bytes(block, 8);
    uint32_t pc = block[1], offset = 8, starts[4], lengths[4], offsets[4];
    for (unsigned i = 0; i < 4; ++i) {
        if (r->size - offset < 8 || io->read(io->context, base + offset, block, 8)) return 0;
        checksum ^= xor_bytes(block, 8);
        offset += 8;
        uint32_t address = block[0], length = block[1];
        if (!length || (length & 3) || (address & 3) || length > r->size - offset) return 0;
        uint32_t end = address + length;
        if (end <= address) return 0;
        // The upper DRAM guard excludes the bootloader's live stack.
        if (i == 0 && address != 0x40200000u + base + offset) return 0;
        if (i == 1 && (address != 0x40100000u || end > 0x40108000u || pc < address || pc >= end || (pc & 3))) return 0;
        if (i >= 2 && (address < 0x3ffe8000u || end > 0x3fff8000u)) return 0;
        if (i == 3 && address < starts[2] + lengths[2]) return 0;
        starts[i] = address; lengths[i] = length; offsets[i] = base + offset;
        offset += length;
    }
    if (r->size != ((offset | 15u) + 1u) || checksum != 0xef ||
        io->read(io->context, base + offset, block, r->size - offset)) return 0;
    const uint8_t *padding = (const uint8_t *)block;
    for (unsigned i=0;i+1<r->size-offset;++i) if (padding[i]) return 0;
    if (load) {
        for (unsigned i = 1; i < 4; ++i) {
            if (io->read(io->context, offsets[i], (void *)(uintptr_t)starts[i], lengths[i])) return 0;
        }
    }
    if (entry) *entry = pc;
    return 1;
}

int ferm_select(const FermBootIO *io) {
    FermBootRecord r[2];
    int confirmed = -1, pending = -1;
    for (unsigned i = 0; i < 2; ++i) {
        if (!ferm_read_record(io, i, &r[i]) || r[i].state == FERM_TRIED ||
            !ferm_image(io, &r[i], 0, NULL)) continue;
        if (r[i].state == FERM_CONFIRMED &&
            (confirmed < 0 || r[i].generation > r[confirmed].generation)) confirmed = (int)i;
        if (r[i].state == FERM_PENDING) pending = (int)i;
    }
    if (confirmed >= 0 && pending >= 0 && r[pending].generation > r[confirmed].generation &&
        ferm_transition(io, (unsigned)pending, FERM_PENDING, FERM_TRIED)) return pending;
    return confirmed;
}
