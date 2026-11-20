// Checks PPU::run_line() against the dot-by-dot PPU::tick(): two PPUs get
// identical VRAM, OAM and register writes during VBlank; one is ticked, the
// other run a line at a time, and their framebuffers must match exactly.
//
//   1. The demo scene, with the window and objects toggled along the way.
//   2. Random fuzz: random VRAM, OAM and registers (8x8/8x16 objects,
//      flips, priorities, both tile data areas and maps, any SCX/SCY/WX/WY).
//
// Run with tests/run.sh, which builds it twice: the desktop configuration
// (framebuffer, full VRAM) and the AVR one (CPPU_LINE_OUTPUT and
// CPPU_COMPACT_VRAM, so the line()/line_palette() path is what's checked).

#include <array>
#include <cstdio>
#include <cstring>
#include <random>

#include "cppu/ppu.hpp"
#include "demo/demo.hpp"

namespace {

// One frame of output, as shades. With CPPU_LINE_OUTPUT (the AVR build)
// it's assembled from line() / line_palette() as each line finishes.
using Frame = std::array<std::uint8_t, cppu::kScreenWidth * cppu::kScreenHeight>;

void collect(cppu::PPU& p, Frame& out) {
#if CPPU_LINE_OUTPUT
    if (p.line_ready()) {
        const int y = p.line_y();
        for (int x = 0; x < cppu::kScreenWidth; ++x) {
            out[y * cppu::kScreenWidth + x] = p.line_palette()[p.line()[x]];
        }
    }
#else
    (void)p;
    (void)out;
#endif
}

void finish(const cppu::PPU& p, Frame& out) {
#if CPPU_LINE_OUTPUT
    (void)p;
    (void)out;
#else
    std::memcpy(out.data(), p.framebuffer(), out.size());
#endif
}

void run_frame_ticks(cppu::PPU& p, Frame& out) {
    do { p.tick(); collect(p, out); } while (!p.frame_ready());
    finish(p, out);
}

void run_frame_lines(cppu::PPU& p, Frame& out) {
    do { p.run_line(); collect(p, out); } while (!p.frame_ready());
    finish(p, out);
}

Frame fa, fb;

int compare(const char* what, int frame) {
    for (int i = 0; i < cppu::kScreenWidth * cppu::kScreenHeight; ++i) {
        if (fa[i] != fb[i]) {
            std::printf("%s frame %d: first mismatch at x=%d y=%d (tick %d, run_line %d)\n",
                        what, frame, i % cppu::kScreenWidth, i / cppu::kScreenWidth, fa[i], fb[i]);
            return 1;
        }
    }
    return 0;
}

int test_demo() {
    cppu::PPU a, b;
    demo::Demo da(a), db(b);
    run_frame_ticks(a, fa);
    run_frame_lines(b, fb);
    for (int f = 0; f < 1500; ++f) {
        demo::Input in;
        in.toggle_window = (f % 400 == 100);
        in.toggle_objects = (f % 500 == 250);
        in.scroll_y = (f / 64) % 3 - 1;
        da.update(in);
        db.update(in);
        run_frame_ticks(a, fa);
        run_frame_lines(b, fb);
        if (compare("demo", f)) return 1;
    }
    std::printf("demo: 1500 frames identical\n");
    return 0;
}

int test_fuzz() {
    std::mt19937 rng(12345);
    auto r8 = [&] { return static_cast<std::uint8_t>(rng() & 0xFF); };
    int failures = 0;
    for (int scene = 0; scene < 300; ++scene) {
        cppu::PPU a, b;
        // LCD off: everything is writable.
        for (std::uint16_t i = 0; i < 0x2000; ++i) {
            // Mostly sparse tile data so objects have transparent pixels.
            const std::uint8_t v = (i < 0x1800 && (rng() % 3 == 0)) ? 0 : r8();
            a.write_vram(i, v);
            b.write_vram(i, v);
        }
        for (int frame = 0; frame < 6 && !failures; ++frame) {
            for (std::uint8_t i = 0; i < 0xA0; ++i) {
                std::uint8_t v = r8();
                // Pull Y/X into the visible range most of the time.
                if (i % 4 == 0 && rng() % 4) v = static_cast<std::uint8_t>(rng() % 170);
                if (i % 4 == 1 && rng() % 4) v = static_cast<std::uint8_t>(rng() % 176);
                a.write_oam_dma(i, v);
                b.write_oam_dma(i, v);
            }
            const std::uint8_t lcdc = static_cast<std::uint8_t>(r8() | cppu::lcdc::kEnable);
            const std::uint8_t regs[][2] = {
                {static_cast<std::uint8_t>(cppu::Reg::SCX), r8()},
                {static_cast<std::uint8_t>(cppu::Reg::SCY), r8()},
                {static_cast<std::uint8_t>(cppu::Reg::WX), static_cast<std::uint8_t>(rng() % 2 ? rng() % 176 : r8())},
                {static_cast<std::uint8_t>(cppu::Reg::WY), static_cast<std::uint8_t>(rng() % 2 ? rng() % 150 : r8())},
                {static_cast<std::uint8_t>(cppu::Reg::BGP), r8()},
                {static_cast<std::uint8_t>(cppu::Reg::OBP0), r8()},
                {static_cast<std::uint8_t>(cppu::Reg::OBP1), r8()},
                {static_cast<std::uint8_t>(cppu::Reg::LCDC), lcdc},
            };
            for (const auto& rv : regs) {
                a.write_register(static_cast<cppu::Reg>(rv[0]), rv[1]);
                b.write_register(static_cast<cppu::Reg>(rv[0]), rv[1]);
            }
            run_frame_ticks(a, fa);
            run_frame_lines(b, fb);
            if (frame > 0) { // frame 0 started mid-LCD-on; compare from frame 1
                char what[32];
                std::snprintf(what, sizeof(what), "fuzz scene %d", scene);
                failures += compare(what, frame);
            }
        }
        if (failures) {
            return 1;
        }
    }
    std::printf("fuzz: 300 scenes x 5 frames identical\n");
    return 0;
}

} // namespace

int main() {
#if CPPU_LINE_OUTPUT && CPPU_COMPACT_VRAM
    std::printf("config: AVR (line output, compact VRAM)\n");
#else
    std::printf("config: desktop (framebuffer, full VRAM)\n");
#endif
    return test_demo() | test_fuzz();
}
