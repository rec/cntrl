// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo.h"
#include "nor.h"

#include <stdio.h>
#include <stdlib.h>
#ifdef CNTRL_DOOM_PROFILE
#include "doomdef.h"
#endif
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

uint8_t demo_rom[NOR_SIZE];
static uint32_t now;
#ifdef CNTRL_DOOM_PROFILE
enum { profile_seconds = 30 };
static uint8_t inputs[profile_seconds * TICRATE + 1];
#else
static uint8_t buttons;
#endif

uint32_t demo_millis(void) { return now; }
void demo_delay(uint32_t ms) { now += ms; }
uint8_t demo_buttons(void)
{
#ifdef CNTRL_DOOM_PROFILE
    const unsigned tic = now * TICRATE / 1000;
    return tic < sizeof(inputs) ? inputs[tic] : 0;
#else
    return buttons;
#endif
}

#ifdef CNTRL_DOOM_PROFILE
void demo_trace_block(uint16_t block)
{
    if (putchar(block & 255) == EOF || putchar(block >> 8) == EOF)
        demo_fail("Could not write Doom asset trace");
}
#endif

void demo_present(const uint16_t *pixels)
{
#ifdef CNTRL_DOOM_PROFILE
    (void)pixels;
    demo_trace_block(UINT16_MAX);
    if (now >= profile_seconds * 1000)
        exit(EXIT_SUCCESS);
#else
    static uint8_t frame[DEMO_WIDTH * DEMO_HEIGHT * 2];
    for (unsigned i = 0; i < DEMO_WIDTH * DEMO_HEIGHT; ++i) {
        frame[2 * i] = pixels[i] >> 8;
        frame[2 * i + 1] = pixels[i];
    }
    if (fwrite(frame, 1, sizeof(frame), stdout) != sizeof(frame) || fflush(stdout))
        demo_fail("Could not send Doom frame");
    int input = getchar();
    if (input == EOF)
        exit(EXIT_SUCCESS);
    buttons = (uint8_t)input;
#endif
}

void demo_fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 ||
        _setmode(_fileno(stdout), _O_BINARY) == -1)
        demo_fail("Could not open binary Doom frame and input streams");
#endif
    if (argc != 2)
        demo_fail("Expected the path to the generated Freedoom image");
    FILE *file = fopen(argv[1], "rb");
    if (!file || fread(demo_rom, 1, sizeof(demo_rom), file) != sizeof(demo_rom) || fgetc(file) != EOF)
        demo_fail("Could not read the generated Freedoom image");
    fclose(file);
#ifdef CNTRL_DOOM_PROFILE
    if (fread(inputs, 1, sizeof(inputs), stdin) != sizeof(inputs))
        demo_fail("Expected 30 seconds of Doom trace input");
#else
    int input = getchar();
    if (input == EOF)
        return EXIT_SUCCESS;
    buttons = (uint8_t)input;
#endif
    demo_run();
    demo_fail("Doom returned unexpectedly");
}
