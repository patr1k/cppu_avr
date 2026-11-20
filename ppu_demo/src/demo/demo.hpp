#pragma once

// Background, window and objects demo scene for the cppu frontend.
//
// Loads a few tiles, a scrolling background map, a window "status bar" and
// a handful of bouncing ball objects into the PPU, then updates the scroll
// and window registers and OAM once per frame.
// It only talks to the PPU through its public interface (VRAM and OAM
// writes, and registers), the same way a game would.

#if !defined(__AVR__)
#include <array>
#include <cstdint>
#endif

#include "../cppu/ppu.hpp"

namespace demo {

// One frame's worth of user input.
struct Input {
    int  scroll_x = 0;          // -1, 0 or +1 (arrow keys)
    int  scroll_y = 0;
    bool toggle_auto_scroll = false;
    bool toggle_window = false;
    bool toggle_objects = false;
};

class Demo {
public:
    // Loads tiles and maps, sets the registers and turns the LCD on.
    explicit Demo(cppu::PPU& ppu);

    // Call once per frame, during VBlank (OAM is locked during modes 2-3
    // and VRAM during mode 3 while the LCD is on).
    void update(const Input& input);

private:
    void write_status_text();
    void update_balls();

    struct Ball {
        double x, y;   // screen position of the top-left corner
        double vx, vy; // pixels per frame
        std::uint8_t attrs;
    };
    static constexpr int kNumBalls = 8;

    cppu::PPU& ppu_;
    int    frame_ = 0;
    double pos_x_ = 0.0;
    double pos_y_ = 0.0;
    bool   auto_scroll_ = true;
    bool   window_shown_ = true;
    bool   objects_shown_ = true;
    int    wy_ = 0;
    std::array<Ball, kNumBalls> balls_{};
};

} // namespace demo
