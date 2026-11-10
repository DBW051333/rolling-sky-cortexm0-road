# RSMENU RLE background video

`RSMENU.RLE` contains the four production flyover backgrounds in this order.
Each scene lasts 20 seconds. The full loop lasts 80 seconds at 30 FPS.
The native canvas is 360 by 640 portrait pixels, with 2400 independently
encoded frames. Titles, menus, skin previews and audio are separate layers.
The existing empirically calibrated geometry is included in the rendering.

The generated file is 23,502,848 bytes (about 22.41 MiB). SHA-256 is
`311d713df266ce3020ab53ce1c7d4779d64eaf9660b1e95b2768da80c40d4377`.
`RSMENU_INFO.json` records per-scene sizes and the checksum.

## Rebuild and validate

From the repository root, with Python, NumPy and a host C compiler available,
run `python tools/make_menu_rle.py`. The script compiles
`tools/render_menu_frames.c` with the production `firmware/flyover.c`, renders
every background frame, encodes it, compares its decoded pixels with the
original, and validates the complete file before publishing it. `CC` can
select the host compiler. Default frame rate is 30; `--fps 20` generates a
20 FPS variant. `--output` selects another output path.

Run `python tools/make_menu_rle.py --verify sd_payload/RSMENU.RLE` to validate
an existing file without rendering it again. The encoder uses a temporary
file so a failed generation preserves an existing completed video.

## Pixel and RLE encoding

Pixels are RGB332, matching the current HDMI framebuffer. Bits 7..5 are red,
4..2 are green, and 1..0 are blue. Frames are stored in native portrait raster
order, left to right and top to bottom, with a 360-byte decoded row stride.
The board's existing rotation and exact 3x3 HDMI presentation are applied
by the player/display pipeline, rather than embedded as another resampling.

Each RLE record consists of two bytes in the order `[length, color]`.
Length ranges from 1 through 255. It repeats that RGB332 color `length`
times. Length zero is invalid. Runs may cross source row boundaries, and
a run longer than 255 pixels is split into successive records. Each frame
must decode to exactly 230400 pixels. RLE preserves all RGB332 pixel values.

Every frame begins at a 512-byte file offset. Encoded lengths exclude the
zero padding between frames. Frames have no inter-frame dependency.

## File header

The first 512 bytes are a little-endian header. Reserved bytes are zero.
CRC32 uses the standard IEEE/zlib CRC32 representation, stored little-endian.

| Byte offset | Field | Value or meaning |
|---|---|---|
| 0 | 8-byte magic | ASCII `RSMENU01` |
| 8 | uint16 version | 1 |
| 10 | uint16 header bytes | 512 |
| 12 | uint16 width | 360 |
| 14 | uint16 height | 640 |
| 16 | uint16 FPS numerator | 30 |
| 18 | uint16 FPS denominator | 1 |
| 20 | uint32 frame count | 2400 |
| 24 | uint32 loop duration in ms | 80000 |
| 28 | uint32 decoded bytes per frame | 230400 |
| 32 | uint32 directory byte offset | 512 |
| 36 | uint16 directory entry bytes | 16 |
| 38 | uint8 pixel format | 1 for RGB332 |
| 39 | uint8 pixel order | 0 for native portrait raster |
| 40 | uint32 first frame byte offset | 38912 |
| 44 | uint32 total file bytes | 23502848 |
| 48 | uint32 directory CRC32 | Covers all 2400 directory entries |
| 508 | uint32 header CRC32 | Covers bytes 0 through 507 |

## Frame directory

The directory contains `frame_count` consecutive 16-byte entries. Each entry
holds four little-endian uint32 values in the following order.

1. Absolute byte offset of the frame payload, aligned to 512 bytes.
2. Encoded payload length, excluding padding.
3. CRC32 of the 230400 decoded RGB332 bytes.
4. CRC32 of the encoded payload.

After the directory, zero padding reaches the next 512-byte boundary.
Frame padding is also zero. Playback time of zero-based frame `i` is
`i * 1000 / 30` milliseconds. After frame 2399, restart from frame zero.
The renderer uses the corresponding integer-millisecond sample times.

## Playback integration

Place `RSMENU.RLE` in the FAT32 card root alongside `RSLEVELS.BIN` and
`RSART.BIN`. The production `MENU_VIDEO` build reads sectors through the CPU
and uploads them in 128-bit chunks to a 4 MiB SDRAM ring. FPGA logic decodes
the RLE records, checks each encoded frame CRC32, and switches background
buffers during HDMI vertical blank. The CPU draws interactive controls into
a separate double-buffered alpha layer. Missing or invalid video uses a
static menu background. Game rendering resumes when leaving the menu.

SDRAM byte addresses are allocated as follows.

| Region | Start | Reserved size |
| --- | --- | --- |
| RGB332 background 0 | 0x000000 | 256 KiB |
| RGB332 background 1 | 0x040000 | 256 KiB |
| Alpha/RGB332 UI 0 | 0x080000 | 512 KiB |
| Alpha/RGB332 UI 1 | 0x100000 | 512 KiB |
| Compressed ring | 0x200000 | 4 MiB |
| Game background 2 | 0x600000 | 256 KiB |

Each UI pixel occupies 16 bits, with alpha in bit 8 and RGB332 in bits 7..0.
The framebuffer region below 2 MiB is cleared at startup. The third game
buffer is fully rendered before its first presentation. Game presentation
uses all three RGB332 buffers, while menu decoding uses buffers 0 and 1.
Eight compressed
frames are queued before enabling playback. SD video reads use 8 MHz SPI,
while existing game reads retain 4 MHz. Video and level FAT cursors are
saved independently. Decoding and presentation continue while the CPU draws
the UI. UI redraws are demand-driven with a 200 ms minimum interval.

Mean payload is 9533 bytes/frame, or about 286 KB/s at 30 FPS, excluding
sector padding. Mean city payload is about 537 KB/s and the largest frame
is 21286 bytes. A prefetch buffer is required to absorb the higher city
bitrate and card latency; a nominal 30 FPS asset does not establish the
board's achieved playback frame rate.
