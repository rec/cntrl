// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.h"

#include <stdio.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomstat.h"
#include "nor.h"
#include "p_mobj.h"

uint8_t demo_rom[NOR_SIZE];
static uint32_t now;
static int last_tic = -1;
static fixed_t start_x, start_y;
static angle_t angle_before_turn;
static int initial_ammo;

uint32_t demo_millis(void) { return now; }
void demo_delay(uint32_t ms)
{
    now += ms;
    if (now > 5000)
        demo_fail("The standalone demo did not finish its input check");
}

uint8_t demo_buttons(void)
{
    // Alternate between one physical pedal held and all pedals released.
    if (leveltime >= 2 && leveltime < 12) return 1u << 1;   // B
    if (leveltime >= 16 && leveltime < 22) return 1u << 2;  // A
    if (leveltime >= 24 && leveltime < 30) return 1u << 0;  // C
    if (leveltime >= 32 && leveltime < 39) return 1u << 4;  // E
    if (leveltime >= 44 && leveltime < 46) return 1u << 5;  // F
    if (leveltime >= 48 && leveltime < 63) return 1u << 3;  // D
    return 0;
}

void demo_present(const uint16_t *pixels)
{
    if (gamestate != GS_LEVEL || gameepisode != 1 || gamemap != 1)
        demo_fail("Expected the standalone demo to start in E1M1");
    if (leveltime == last_tic)
        return;
    const player_t *player = &players[0];
    if (last_tic < 0) {
        start_x = player->mo->x;
        start_y = player->mo->y;
        angle_before_turn = player->mo->angle;
        initial_ammo = player->ammo[am_clip];
    }
    last_tic = leveltime;
    switch (leveltime) {
    case 13:
        if (player->mo->x == start_x && player->mo->y == start_y)
            demo_fail("Holding B did not move the player");
        if (player->cmd.forwardmove)
            demo_fail("Releasing B did not clear forward input");
        break;
    case 23:
        if ((int32_t)(player->mo->angle - angle_before_turn) <= 0)
            demo_fail("Holding A did not turn the player left");
        angle_before_turn = player->mo->angle;
        break;
    case 31:
        if ((int32_t)(player->mo->angle - angle_before_turn) >= 0)
            demo_fail("Holding C did not turn the player right");
        break;
    case 36:
        if (player->cmd.forwardmove >= 0)
            demo_fail("Holding E did not request backward movement");
        break;
    case 46:
        if (!player->usedown)
            demo_fail("Holding F did not activate use");
        break;
    case 48:
        if (player->usedown)
            demo_fail("Releasing F did not clear use");
        break;
    case 64:
        if (player->ammo[am_clip] >= initial_ammo)
            demo_fail("Holding D did not fire the pistol");
        break;
    }
    if (leveltime != 64)
        return;

    for (unsigned i = 1; i < DEMO_WIDTH * DEMO_HEIGHT; ++i) {
        if (pixels[i] != pixels[0]) {
            puts("E1M1 rendered through tic 64 and responded to all six pedals");
            exit(EXIT_SUCCESS);
        }
    }
    demo_fail("The rendered frame is a single color");
}

void demo_fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
    if (argc != 2)
        demo_fail("Expected the path to the generated Freedoom image");
    FILE *file = fopen(argv[1], "rb");
    if (!file || fread(demo_rom, 1, sizeof(demo_rom), file) != sizeof(demo_rom))
        demo_fail("Could not read the generated Freedoom image");
    fclose(file);
#ifdef CNTRL_DOOM_FLASH_CACHE
    if (!demo_verify_flash_cache())
        demo_fail("The Doom flash cache does not match the asset image");
#endif
    demo_run();
    demo_fail("Doom returned before rendering the test frames");
}
