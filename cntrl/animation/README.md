# Animation streams

The compiler supplies `Geometry(width, height, streams)` from the target descriptor.
Each logical stream has explicit horizontal and vertical mirror transforms.
Pixels are packed RGB565 with the most significant byte first. Image preparation
zooms to 120% of the supplied dimensions and crops centrally. Missing or zero
animation delays become 100 ms; untimed still images use 1,000 ms.

CNSA version 2 starts with a 20-byte little-endian header:

| Offset | Field |
| --- | --- |
| 0 | Four bytes `CNSA` |
| 4 | uint16 version, 2 |
| 6 | uint16 width |
| 8 | uint16 height |
| 10 | uint16 stream count |
| 12 | uint16 frame count |
| 14 | uint16 pixel format, 1 for RGB565 |
| 16 | uint32 total asset size |

Each frame starts with a uint32 duration in milliseconds, followed by pixels in
row, stream, pixel order. Dimensions and counts must be nonzero. Durations range
from 1 to 2,147,483,647 ms. Size arithmetic is checked before playback or upload.
Version-1 conversion requires the caller to supply its known geometry and stream
mapping. It preserves existing pixel streams and frame durations exactly.

The portable player depends only on `Source` and `Output`. `Output.begin` accepts
or rejects the declared dimensions and logical stream count. Each `poll` reads
and submits at most 320 bytes. `ready()` gates reuse of the player's retained
packet; `pixels` returns false without retaining the packet if it cannot accept
that region. The output adapter maps stream IDs and logical coordinates to its
physical surfaces and handles byte-order conversion where required. There is no
full-frame buffer. Transfer verification processes at most 512 bytes per poll.
