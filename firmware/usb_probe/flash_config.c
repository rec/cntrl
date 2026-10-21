#include <stdint.h>

// NXP RT1062 boot-ROM configuration, reconstructed as hardware data from the
// known-good MC6 Pro v3.14.0 header. See ../flash-configuration.md for decoding.
// Unspecified words, including device-mode configuration enables, remain zero.
__attribute__((section(".flashconfig"), used))
const uint32_t FlexSPI_NOR_Config[128] = {
    [0x000 / 4] = 0x42464346, // FCFB tag.
    [0x004 / 4] = 0x56010000, // Format V1.0.0.
    [0x00c / 4] = 0x00030301, // DQS-pad loopback, CS hold/setup 3/3.
    [0x044 / 4] = 0x00080401, // Serial NOR, 4 pads, 166 MHz enum, no custom sequences.
    [0x050 / 4] = 0x00800000, // 8 MiB on A1.
    [0x080 / 4] = 0x0a1804eb, // Read: CMD 0xeb on 1 pad; address 24 bits on 4 pads.
    [0x084 / 4] = 0x26043206, // Dummy 6 cycles on 4 pads; read 4 bytes on 4 pads.
    [0x090 / 4] = 0x24040405, // Read status: CMD 0x05; read 4 bytes, both 1 pad.
    [0x0b0 / 4] = 0x00000406, // Write enable: CMD 0x06 on 1 pad; STOP.
    [0x0d0 / 4] = 0x08180420, // Sector erase: CMD 0x20; 24-bit address, 1 pad.
    [0x100 / 4] = 0x081804d8, // Block erase: CMD 0xd8; 24-bit address, 1 pad.
    [0x110 / 4] = 0x08180402, // Page program: CMD 0x02; 24-bit address, 1 pad.
    [0x114 / 4] = 0x00002004, // Write 4 bytes on 1 pad; STOP.
    [0x130 / 4] = 0x00000460, // Chip erase: CMD 0x60 on 1 pad; STOP.
    [0x1c0 / 4] = 256,        // Page size in bytes.
    [0x1c4 / 4] = 4096,       // Sector size in bytes.
    [0x1c8 / 4] = 1,          // IP-command clock enum: 30 MHz.
    [0x1d0 / 4] = 65536,      // Block size in bytes.
};

_Static_assert(sizeof(FlexSPI_NOR_Config) == 512, "NXP flash config must be 512 bytes");
