// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DEMO_WIDTH = 160, DEMO_HEIGHT = 128 };
int demo_read_assets(uint32_t offset, void *dest, uint32_t count);
#ifdef CNTRL_DOOM_FLASH_CACHE
int demo_verify_flash_cache(void);
#endif
#ifdef CNTRL_DOOM_PROFILE
void demo_trace_block(uint16_t block);
#endif
void demo_run(void);
uint32_t demo_millis(void);
void demo_delay(uint32_t ms);
uint8_t demo_buttons(void);
void demo_present(const uint16_t *pixels);
void demo_fail(const char *message) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
