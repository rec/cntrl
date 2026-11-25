// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { DEMO_WIDTH = 160, DEMO_HEIGHT = 128 };
int demo_read_assets(uint32_t offset, void *dest, uint32_t count);
void demo_run(void);
uint32_t demo_millis(void);
void demo_delay(uint32_t ms);
uint8_t demo_buttons(void);
void demo_present(const uint16_t *pixels);
void demo_fail(const char *message) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif
