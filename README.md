# avrboy

Parts of the Game Boy (DMG) on an Arduino Mega2560 with a 480x320 HX8357 TFT
shield ([TFT_HX8357](https://github.com/Bodmer/TFT_HX8357)).

## ppu_demo

Runs the [cppu](https://github.com/patr1k/cppu) PPU and its demo scene (the
same one the SDL frontend shows: scrolling BG, window status bar, bouncing
objects). By default the PPU runs a whole line at a time (`run_line()`, see
below). Each scanline is sent to the TFT as soon as the PPU finishes it. The picture is drawn at 2x (320x288,
centred). Set `SCALE` in `ppu_demo.ino` to 1 for native size.

Serial (115200) prints a raw TFT benchmark at startup, then the average
time per frame for the scene update, the PPU and the TFT. The frame rate is
also drawn under the picture.

### Build / upload

```
./build.sh            # compile, upload to COM3, open the serial monitor
./build.sh compile
PORT=COM4 OPT=-Os ./build.sh upload
```

This uses the `arduino-cli.exe` that ships with the Windows Arduino IDE, so
uploads go through the Windows COM port (no usbipd needed). It passes
`-std=gnu++17` (cppu needs C++17; the AVR core defaults to gnu++11) and
`-O2` in place of the core's `-Os`. The plain Arduino IDE won't build this
sketch without those flags.

### Changes from upstream cppu (`ceada7f`)

`ppu_demo/src/cppu` and `ppu_demo/src/demo` are copies of cppu's
`include/cppu`, `src/ppu.cpp` and `frontend/demo.*`. All the changes are
behind `#if` blocks, so the copies still build on the desktop:

- **`avr_std.hpp`**: avr-gcc has no C++ standard library, so this file
  provides small versions of `std::array`, `std::exchange` and the
  `<cstdint>`/`<cmath>`/`<cstdio>` names that cppu uses.
- **`CPPU_LINE_OUTPUT`**: a 160-byte line buffer takes the place of the
  23 KB framebuffer. After each line, `line_ready()` is true, `line()` holds
  160 palette indices for row `line_y()`, and `line_palette()` maps each
  index to a shade. Indices 0-3 are BG/window colours through BGP, 4-7
  object colours through OBP0, 8-11 through OBP1. `tick()` writes shades
  directly, with an identity `line_palette()`.
- **`CPPU_COMPACT_VRAM`**: only VRAM 0x0000-0x07FF (tiles 0-127) and
  0x1800-0x1FFF (both tile maps) are stored, which takes 4 KB instead of
  8 KB. Signed tile addressing (LCDC.4 = 0) won't work. The demo doesn't
  use it.
- **`PPU::run_line()`** (new): runs one whole line (456 dots) per call and
  draws it in a single pass: OAM scan, BG, window, then objects in the order
  the FIFO renderer would fetch them (max(X - 8, 0), ties broken by OAM
  index). It skips the dot-by-dot pixel FIFOs. LY, modes, the WY latch, the
  window line counter and VBlank/STAT interrupts advance the same way, but
  only per line: mode 3 length isn't modelled. Its output matches `tick()`
  exactly as long as nothing changes mid-line. To use `tick()` instead, set
  `RENDER_BY_LINE 0` in the sketch.
- **`tile_decode.hpp`** (new): decodes one tile row into 8 colours. On AVR
  it's 6 instructions of inline assembly per pixel (7 cycles). Everywhere
  else it's the plain C reference version.
- `end_line()` advances LY with a compare instead of `% kLinesPerFrame`,
  which on AVR was a 16-bit division call on every line.
- `kDotsPerFrame` is now `int32_t`, because 70224 doesn't fit in the AVR's
  16-bit `int`.

The whole `cppu::PPU` object is 4608 bytes. Globals use 5.6 KB of the 8 KB
SRAM.

### Tests

`tests/run.sh` builds and runs `tests/scanline_equiv.cpp` twice: once in
the desktop configuration (framebuffer, full VRAM) and once in the AVR one
(`CPPU_LINE_OUTPUT` + `CPPU_COMPACT_VRAM`, so the `line()` /
`line_palette()` path is what gets checked). Each run puts a `tick()` PPU
and a `run_line()` PPU side by side and checks that every frame is
identical. It covers 1500 frames of the demo (with the window and objects
toggled) and 300 random scenes (random VRAM/OAM/registers, 8x16 objects,
flips, priorities, both tile data areas and maps).

The desktop builds use the C tile decoder. The AVR assembly version is
checked on the Mega itself: at startup the sketch compares it with the C
version for all 65,536 possible pairs of bitplane bytes and prints
`decode self-test: all 65536 tile rows match`.

### Results (Mega2560 @ 16 MHz, -O2, SCALE 2)

| What | `tick()` | `run_line()` |
|---|---|---|
| PPU, one frame | 819 ms (~187 cycles/dot) | 33.7 ms (~3,750 cycles/line) |
| TFT, one GB frame at 2x (92,160 px) | 65.6 ms | 65.6 ms |
| Scene update | 1 ms | 1 ms |
| **Total** | **~886 ms (1.1 fps)** | **100.4 ms (10.0 fps)** |

The `tick()` column's PPU figure is from before the TFT changes, with the
current TFT time added. At SCALE 1 the demo runs at **15.5 fps** (see
below).

### PPU optimisation steps

All measured on the Mega with the demo scene, SCALE 2:

| Step | PPU per frame | Cycles per line |
|---|---|---|
| `tick()`, dot by dot | 819 ms | ~91,000 |
| First `run_line()` | 99.1 ms | ~11,000 |
| Output palette indices instead of shades, which drops the separate palette and object-mix passes over every pixel; objects write straight into the line; tile map read through a pointer; 8-bit OAM scan; no `%` in `end_line()` | 45.5 ms | ~5,050 |
| Tile decode in AVR assembly; line palette built by stepping the palette registers 2 bits at a time (variable shifts are loops on AVR) | 33.7 ms | ~3,750 |

Before the last step, a Timer1 profile of the remaining ~5,000 cycles per
line put the tile decode at ~3,400, the OAM scan at ~560, the line palette
at ~380, objects at ~370, and mode/line bookkeeping at ~220. The assembly
decode brings the per-pixel part of that down to 7 cycles. I haven't
profiled again since then.

### TFT output (`draw_line`)

`draw_line()` in `ppu_demo.ino` sends one GB scanline per call. It calls the
library's `setWindow()` once to set the address window to that line's
`SCALE` rows, then writes the pixels straight to the shield's data ports
(PORTA = high byte, PORTC = low byte, strobe WR). This is the same protocol
the library's `pushColors()` uses, written as a short inline-assembly loop.
Once per line, it folds `line_palette()` and the RGB565 colours into a
32-byte-aligned table of port bytes. Looking up an index is then one `add`
into the table's low address byte plus two loads: 17 cycles per GB pixel,
+4 at 2x for the second strobe (the colour is still on the bus). At 2x the
line is sent twice, once for each TFT row. `pushColors()` can't take a
whole 2x line (320 pixels) in one call anyway, since its length is a
`uint8_t`.

The baseline, `draw_line_pushcolors()`, draws the same line with the
library's public API. It sets the same window, expands each TFT row into a
160-entry RGB565 buffer (index → shade → colour), and sends that with
`pushColors(uint16_t*, len)`. The startup benchmark times both drawers on a
synthetic frame. The time spent building the synthetic lines (23.4 ms) is
measured on its own and subtracted. Set `TFT_USE_PUSHCOLORS 1` in the
sketch to run the demo itself on the baseline.

**SCALE 2** (320x288, 92,160 TFT px per frame):

| One GB frame | `pushColors()` (library) | `draw_line` (direct ports) | Speed-up |
|---|---|---|---|
| Startup benchmark (net) | 286.1 ms (0.32 Mpx/s) | 64.9 ms (1.42 Mpx/s) | 4.4x |
| Demo, TFT time per frame | 286.8 ms | 65.6 ms | 4.4x |
| Demo, total (with `run_line()`) | 321.6 ms = **3.1 fps** | 100.4 ms = **10.0 fps** | 3.2x |

**SCALE 1** (160x144 native, 23,040 TFT px per frame):

| One GB frame | `pushColors()` (library) | `draw_line` (direct ports) | Speed-up |
|---|---|---|---|
| Startup benchmark (net) | 63.6 ms (0.36 Mpx/s) | 29.0 ms (0.80 Mpx/s) | 2.2x |
| Demo, TFT time per frame | 64.3 ms | 29.7 ms | 2.2x |
| Demo, total (with `run_line()`) | 99.1 ms = **10.1 fps** | 64.5 ms = **15.5 fps** | 1.5x |

The PPU's 33.7 ms per frame is the same at both scales.

Per TFT pixel, `draw_line` costs about 11 CPU cycles at SCALE 2 and about
21 at SCALE 1. At 2x, each colour is put on the bus once and then strobed
twice, so the table lookups and port writes are shared by two pixels. At 1x
every pixel pays for them. That's why SCALE 1 draws a quarter of the pixels
but only cuts the TFT time from 66 ms to 30 ms. The speed-up over
`pushColors()` is smaller at 1x for the same reason. The baseline costs
about the same per pixel at both scales (~50 vs ~45 cycles), because it
handles every TFT pixel separately. `draw_line` gets its extra advantage at
2x from the double strobe.

For an upper bound, the library's `fillScreen` (480x320, one colour) takes
42.5 ms, or 3.6 Mpx/s. It only toggles WR and never changes the data lines,
which is the most this bus can do from the Mega. At that rate one 2x GB
frame would take ~25 ms, so `draw_line` is within about 2.6x of the
hardware limit. None of these TFT figures depend on which PPU renderer is
used.
