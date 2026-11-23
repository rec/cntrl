// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.h"

#include <stdarg.h>
#include <stdio.h>

#include "doomdef.h"
#include "doomkeys.h"
#include "d_event.h"
#include "d_main.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_timer.h"
#include "m_argv.h"
#include "ovl.h"
#include "p_mobj.h"
#include "pad.h"
#include "platform.h"
#include "sounds.h"

extern int detailLevel;
int dmc_render = 1;
volatile pad_t dmc_pad; // The analog gamepad stays disconnected.
const ovl_fn ovl_tab_AUTOMAP[] = {NULL};
const uint16_t ovl_cnt_AUTOMAP = 0;

int II_PadAxis(int value, int max) { (void)value; (void)max; return 0; }
void HS_Event(int event, const mobj_t *thing) { (void)event; (void)thing; }

void demo_run(void)
{
    static char name[] = "mc6-doom", warp[] = "-warp", one[] = "1";
    static char *args[] = {name, warp, one, one, NULL};
    myargv = args;
    myargc = 4;
    detailLevel = 1;
    D_DoomMain();
}

void I_StartTic(void)
{
    // Board bits 2,1,0,3,4,5 are pedals A through F, already debounced.
    static const int keys[] = {KEY_RIGHTARROW, KEY_UPARROW, KEY_LEFTARROW,
                               KEY_FIRE, KEY_DOWNARROW, KEY_USE};
    static uint8_t previous;
    uint8_t current = demo_buttons();
    for (unsigned bit = 0; bit < sizeof(keys) / sizeof(*keys); ++bit) {
        unsigned mask = 1u << bit;
        if ((current ^ previous) & mask) {
            event_t event = {.type = current & mask ? ev_keydown : ev_keyup,
                             .data1 = keys[bit]};
            D_PostEvent(&event);
        }
    }
    previous = current;
}

void I_InitTimer(void) {}
int I_GetTimeMS(void) { return (int)demo_millis(); }
int I_GetTime(void)
{
    uint32_t ms = demo_millis();
    return (int)(ms / 1000 * TICRATE + ms % 1000 * TICRATE / 1000);
}
void I_Sleep(int ms) { demo_delay(ms > 0 ? (uint32_t)ms : 1); }
void I_WaitVBL(int count) { demo_delay((uint32_t)count * 1000 / 70); }

void I_Error(char *fmt, ...)
{
    char msg[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    demo_fail(msg);
}
void dmc_fatal(const char *msg) { demo_fail(msg); }
void plat_panic(const char *msg) { demo_fail(msg); }
void I_Quit(void) { demo_fail("Doom exited"); }

int plat_fmt(char *out, int cap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int size = vsnprintf(out, (size_t)cap, fmt, ap);
    va_end(ap);
    return size;
}

byte *I_ZoneBase(int *size)
{
    static _Alignas(8) byte zone[32 * 1024];
    *size = sizeof(zone);
    return zone;
}

void I_Tactile(int on, int off, int total) { (void)on; (void)off; (void)total; }
void I_InitSound(boolean prefix) { (void)prefix; }
void I_UpdateSound(void) {}
int I_GetSfxLumpNum(sfxinfo_t *sfx) { return (int)(sfx - S_sfx); }
int I_StartSound(sfxinfo_t *sfx, int channel, int vol, int sep)
{
    (void)sfx; (void)channel; (void)vol; (void)sep;
    return -1;
}
void I_StopSound(int handle) { (void)handle; }
boolean I_SoundIsPlaying(int handle) { (void)handle; return false; }
void I_UpdateSoundParams(int handle, int vol, int sep) { (void)handle; (void)vol; (void)sep; }
