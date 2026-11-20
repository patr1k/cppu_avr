// avrboy ppu_demo: runs the cppu PPU and its background/window/objects demo
// scene on an Arduino Mega2560, drawing each scanline to an HX8357 TFT as
// the PPU finishes it, and reports how long the PPU and the TFT take.
//
// 1. At startup, a raw TFT benchmark (no PPU): full-screen fills, then a
//    synthetic 160x144 frame drawn with draw_line() (direct port writes)
//    and with the library's pushColors() for comparison.
// 2. Then the demo runs forever. Every kReportFrames frames it prints the
//    average PPU, TFT and scene-update time per frame over Serial (115200)
//    and shows the frame rate under the picture.

#include <TFT_HX8357.h>

#include "src/cppu/ppu.hpp"
#include "src/cppu/tile_decode.hpp"
#include "src/demo/demo.hpp"

// 1 = 160x144 (native), 2 = 320x288 (each GB pixel drawn as 2x2).
#define SCALE 2

// 1 = PPU::run_line() (whole line per call), 0 = PPU::tick() (dot by dot,
// pixel FIFOs). Both draw the same pixels; see tests/scanline_equiv.cpp.
#define RENDER_BY_LINE 1

// 0 = draw_line() (direct port writes), 1 = draw_line_pushcolors() (the
// library's pushColors(), as a baseline).
#define TFT_USE_PUSHCOLORS 0

namespace {

constexpr int16_t kTftW = 480; // landscape (rotation 1)
constexpr int16_t kTftH = 320;
constexpr int16_t kGbW = cppu::kScreenWidth;
constexpr int16_t kGbH = cppu::kScreenHeight;
constexpr int16_t kOriginX = (kTftW - kGbW * SCALE) / 2;
constexpr int16_t kOriginY = (kTftH - kGbH * SCALE) / 2;
constexpr int16_t kStatusY = kOriginY + kGbH * SCALE; // text line below the picture

constexpr uint8_t kReportFrames = 10;

constexpr uint16_t rgb565(uint32_t rgb) {
    return static_cast<uint16_t>(((rgb >> 8) & 0xF800) | ((rgb >> 5) & 0x07E0) | ((rgb >> 3) & 0x001F));
}

// DMG-style green palette (same as the SDL frontend), indexed by shade 0-3.
// Split into the bytes for the two 8-bit data ports: PORTA = high, PORTC = low.
constexpr uint16_t kPalette[4] = {
    rgb565(0x9BBC0F), rgb565(0x8BAC0F), rgb565(0x306230), rgb565(0x0F380F),
};
uint8_t shade_hi[4];
uint8_t shade_lo[4];

// Line palette for synthetic lines that already hold shades 0-3.
const uint8_t kIdentityPalette[cppu::kLinePaletteSize] = {0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};

TFT_HX8357 tft;
cppu::PPU ppu;
demo::Demo scene(ppu);

// Port bytes for each palette index of the current line: high bytes at
// [0, 12), low bytes at [16, 28). 32-byte alignment means the address of
// entry i is just the table address with i added to its low byte.
uint8_t port_tab[32] __attribute__((aligned(32)));

// Draws GB scanline `y`. `px` holds palette indices and `pal` maps each one
// to a shade (see PPU::line_palette()); both are folded into port_tab once
// per line. Sets the TFT address window to the line's SCALE rows, then
// streams the pixels straight to the data ports: the same protocol as the
// library's pushColors(), as a hand-written AVR loop (17 cycles per pixel,
// +4 for the second strobe at SCALE 2).
void draw_line(uint8_t y, const uint8_t* px, const uint8_t* pal) {
    for (uint8_t i = 0; i < cppu::kLinePaletteSize; ++i) {
        port_tab[i] = shade_hi[pal[i]];
        port_tab[16 + i] = shade_lo[pal[i]];
    }
    const int16_t sy = kOriginY + y * SCALE;
    tft.setWindow(kOriginX, sy, kOriginX + kGbW * SCALE - 1, sy + SCALE - 1);
    CS_L;
    const uint8_t base = static_cast<uint8_t>(reinterpret_cast<uintptr_t>(port_tab));
    for (uint8_t row = 0; row < SCALE; ++row) {
        const uint8_t* p = px;
        const uint8_t* z = port_tab;
        uint8_t n = kGbW;
        uint8_t t;
        asm volatile(
            "1:  ld   %[t], %a[p]+        \n\t" // index
            "    mov  %A[z], %[base]      \n\t" // Z = &port_tab[index]
            "    add  %A[z], %[t]         \n\t"
            "    ld   %[t], %a[z]         \n\t"
            "    out  %[porta], %[t]      \n\t" // high byte
            "    ldd  %[t], %a[z]+16      \n\t"
            "    out  %[portc], %[t]      \n\t" // low byte
            "    cbi  %[portg], 2         \n\t" // WR strobe
            "    sbi  %[portg], 2         \n\t"
#if SCALE == 2
            "    cbi  %[portg], 2         \n\t" // same colour, next pixel
            "    sbi  %[portg], 2         \n\t"
#endif
            "    dec  %[n]                \n\t"
            "    brne 1b                  \n\t"
            : [p] "+x"(p), [z] "+z"(z), [n] "+r"(n), [t] "=&r"(t)
            : [base] "r"(base),
              [porta] "I"(_SFR_IO_ADDR(PORTA)),
              [portc] "I"(_SFR_IO_ADDR(PORTC)),
              [portg] "I"(_SFR_IO_ADDR(PORTG)));
    }
    CS_H;
}

// Baseline for comparison: the same line drawn with the library's public
// API. pushColors() takes RGB565 values, at most 255 per call, so each TFT
// row is expanded into a 160-entry colour buffer and sent in chunks.
void draw_line_pushcolors(uint8_t y, const uint8_t* px, const uint8_t* pal) {
    const int16_t sy = kOriginY + y * SCALE;
    tft.setWindow(kOriginX, sy, kOriginX + kGbW * SCALE - 1, sy + SCALE - 1);
    constexpr uint8_t kChunk = 160; // kGbW * SCALE is a multiple of this
    uint16_t buf[kChunk];
    for (uint8_t row = 0; row < SCALE; ++row) {
        for (uint16_t i = 0; i < kGbW * SCALE; i += kChunk) {
            for (uint8_t j = 0; j < kChunk; ++j) {
                buf[j] = kPalette[pal[px[(i + j) / SCALE]]];
            }
            tft.pushColors(buf, kChunk);
        }
    }
}

// Which line drawer the demo uses (the startup benchmark times both).
#if TFT_USE_PUSHCOLORS
#define DEMO_DRAW_LINE draw_line_pushcolors
#else
#define DEMO_DRAW_LINE draw_line
#endif

void print_rate(const __FlashStringHelper* what, uint32_t us, uint32_t pixels) {
    Serial.print(what);
    Serial.print(us / 1000.0, 2);
    Serial.print(F(" ms  ("));
    Serial.print(pixels * 1000.0 / us, 0);
    Serial.println(F(" kpx/s)"));
}

using LineFn = void (*)(uint8_t y, const uint8_t* px, const uint8_t* pal);

void noop_line(uint8_t, const uint8_t* px, const uint8_t*) {
    // Keep the synthetic line from being optimised away.
    asm volatile("" : : "r"(px) : "memory");
}

// Average time per frame to build a synthetic frame (vertical bands, rolling
// each frame) line by line and hand each line to `fn`.
uint32_t time_synthetic_frames(LineFn fn) {
    constexpr uint8_t kFrames = 5;
    uint8_t line[kGbW];
    const uint32_t t0 = micros();
    for (uint8_t f = 0; f < kFrames; ++f) {
        for (uint8_t y = 0; y < kGbH; ++y) {
            for (uint8_t x = 0; x < kGbW; ++x) {
                line[x] = ((x + y + f * 8) >> 3) & 3;
            }
            fn(y, line, kIdentityPalette);
        }
    }
    return (micros() - t0) / kFrames;
}

// The PPU's AVR assembly tile decoder against its C reference, for every
// possible pair of bitplane bytes (~1 s).
bool decode_self_test() {
    uint8_t a[8], b[8];
    uint8_t lo = 0;
    do {
        uint8_t hi = 0;
        do {
            cppu::decode_tile_row(a, lo, hi);
            cppu::decode_tile_row_ref(b, lo, hi);
            if (memcmp(a, b, 8) != 0) {
                Serial.print(F("decode self-test FAILED at lo="));
                Serial.print(lo);
                Serial.print(F(" hi="));
                Serial.println(hi);
                return false;
            }
        } while (++hi != 0);
    } while (++lo != 0);
    Serial.println(F("decode self-test: all 65536 tile rows match"));
    return true;
}

void tft_benchmark() {
    Serial.println(F("--- raw TFT benchmark ---"));

    constexpr uint8_t kFills = 4;
    const uint16_t fill_colors[kFills] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_BLACK};
    uint32_t t0 = micros();
    for (uint8_t i = 0; i < kFills; ++i) {
        tft.fillScreen(fill_colors[i]);
    }
    print_rate(F("fillScreen 480x320 (library):  "), (micros() - t0) / kFills, 480UL * 320);

    // Building the synthetic lines costs the same for both drawers, so
    // time it on its own and report each drawer net of it.
    const uint32_t build_us = time_synthetic_frames(noop_line);
    const uint32_t fast_us = time_synthetic_frames(draw_line) - build_us;
    const uint32_t lib_us = time_synthetic_frames(draw_line_pushcolors) - build_us;
    constexpr uint32_t kPixels = 1UL * kGbW * kGbH * SCALE * SCALE;

    Serial.print(F("GB frame at SCALE "));
    Serial.print(SCALE);
    Serial.print(F(", net of building the lines ("));
    Serial.print(build_us / 1000.0, 2);
    Serial.println(F(" ms):"));
    print_rate(F("  draw_line (direct ports):    "), fast_us, kPixels);
    print_rate(F("  pushColors (library):        "), lib_us, kPixels);
}

} // namespace

void setup() {
    Serial.begin(115200);
    Serial.println(F("\navrboy ppu_demo"));
    Serial.println(RENDER_BY_LINE ? F("renderer: run_line (whole line)") : F("renderer: tick (dot by dot)"));
    Serial.println(TFT_USE_PUSHCOLORS ? F("TFT: pushColors (library)") : F("TFT: draw_line (direct ports)"));
    Serial.print(F("sizeof(cppu::PPU) = "));
    Serial.println(sizeof(cppu::PPU));

    for (uint8_t i = 0; i < 4; ++i) {
        shade_hi[i] = kPalette[i] >> 8;
        shade_lo[i] = kPalette[i] & 0xFF;
    }

    decode_self_test();
    tft.init();
    tft.setRotation(1);
    tft_benchmark();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    // Run up to the first VBlank, so every scene update happens during
    // VBlank, as in the SDL frontend. Lines of this first frame are dropped.
    while (!ppu.frame_ready()) {
        ppu.tick();
    }
    ppu.line_ready();

    Serial.println(F("--- demo (averages per frame) ---"));
}

void loop() {
    static uint8_t frames = 0;
    static uint32_t update_us = 0, ppu_us = 0, lcd_us = 0;

    uint32_t t0 = micros();
    scene.update(demo::Input{});
    uint32_t t1 = micros();
    update_us += t1 - t0;

    uint32_t frame_lcd_us = 0;
    do {
#if RENDER_BY_LINE
        ppu.run_line();
#else
        ppu.tick();
#endif
        if (ppu.line_ready()) {
            const uint32_t a = micros();
            DEMO_DRAW_LINE(ppu.line_y(), ppu.line(), ppu.line_palette());
            frame_lcd_us += micros() - a;
        }
    } while (!ppu.frame_ready());
    lcd_us += frame_lcd_us;
    ppu_us += (micros() - t1) - frame_lcd_us;

    if (++frames < kReportFrames) {
        return;
    }

    const uint32_t total_us = update_us + ppu_us + lcd_us;
    Serial.print(F("update "));
    Serial.print(update_us / 1000.0 / frames, 1);
    Serial.print(F(" ms | PPU "));
    Serial.print(ppu_us / 1000.0 / frames, 1);
    Serial.print(F(" ms ("));
    // CPU cycles per visible line at 16 MHz
    Serial.print(ppu_us * 16.0 / (static_cast<float>(cppu::kScreenHeight) * frames), 0);
    Serial.print(F(" cyc/line) | TFT "));
    Serial.print(lcd_us / 1000.0 / frames, 1);
    Serial.print(F(" ms | total "));
    Serial.print(total_us / 1000.0 / frames, 1);
    Serial.print(F(" ms = "));
    Serial.print(1e6 * frames / total_us, 2);
    Serial.println(F(" fps"));

    char buf[64];
    char fps[8];
    dtostrf(1e6 * frames / total_us, 4, 2, fps);
    snprintf(buf, sizeof(buf), "%s fps  PPU %lu ms  TFT %lu ms   ", fps,
             ppu_us / 1000UL / frames, lcd_us / 1000UL / frames);
    tft.drawString(buf, kOriginX, kStatusY, 2);

    frames = 0;
    update_us = ppu_us = lcd_us = 0;
}
