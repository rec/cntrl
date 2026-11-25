// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.h"

#include <string.h>

#include "dmc_fw.h"
#include "nor.h"
#include "platform.h"

#ifndef ARDUINO
extern const uint8_t demo_rom[NOR_SIZE];

int demo_read_assets(uint32_t offset, void *dest, uint32_t count)
{
    memcpy(dest, demo_rom + offset, count);
    return 1;
}
#endif

enum { block_size = 4096, cache_slots = 16 };
static struct {
    uint32_t block_plus_one;
    uint8_t bytes[block_size];
} cache[cache_slots];

dmc_set_t dmc_set;

void nor_read(uint32_t offset, void *dest, uint32_t count)
{
    if (offset > NOR_SIZE || count > NOR_SIZE - offset)
        demo_fail("Read outside the Freedoom image");
    uint8_t *out = dest;
    while (count) {
        const uint32_t block = offset / block_size;
        const uint32_t start = offset % block_size;
        uint32_t length;
        if (!start && count >= block_size) {
            length = count - count % block_size;
            if (!demo_read_assets(offset, out, length))
                demo_fail("Could not read the Freedoom image");
        } else {
            const unsigned slot = block % cache_slots;
            if (cache[slot].block_plus_one != block + 1) {
                if (!demo_read_assets(block * block_size, cache[slot].bytes, block_size))
                    demo_fail("Could not read the Freedoom image");
                cache[slot].block_plus_one = block + 1;
            }
            length = block_size - start;
            if (length > count) length = count;
            memcpy(out, cache[slot].bytes + start, length);
        }
        offset += length; out += length; count -= length;
    }
}

int nor_read_async(uint32_t offset, void *dest, uint32_t count, int priority)
{
    (void)priority;
    nor_read(offset, dest, count);
    return 1;
}

int nor_col_start(uint32_t offset, void *dest, uint32_t count)
{
    if (count == 0 || count > 256)
        return -1;
    nor_read(offset, dest, count);
    return 0;
}

void nor_col_wait(void) {}
void nor_wait_all(void) {}
void nor_wait(int ticket) { (void)ticket; }
bool nor_done(int ticket) { (void)ticket; return true; }
int nor_erase(uint32_t offset, unsigned cmd) { (void)offset; (void)cmd; return -1; }
int nor_program(uint32_t offset, const void *src, uint32_t count)
{
    (void)offset; (void)src; (void)count;
    return -1;
}
int settings_save(const void *src, uint32_t size, uint16_t version)
{
    (void)src; (void)size; (void)version;
    return -1;
}
void snd_volume(int volume) { (void)volume; }
void rc_stat_hook(uint32_t offset, int hit) { (void)offset; (void)hit; }
void img_stat_hook(uint32_t offset, uint32_t size, uint32_t reads)
{
    (void)offset; (void)size; (void)reads;
}
int dmc_demo_open(const char *name, uint32_t *size) { (void)name; (void)size; return -1; }
void dmc_demo_read(uint32_t offset, void *dest, uint32_t size)
{
    (void)offset; (void)dest; (void)size;
    demo_fail("External demo files are unavailable");
}
