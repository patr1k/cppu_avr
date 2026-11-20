#include "demo.hpp"

#if !defined(__AVR__)
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#endif

namespace demo {

namespace {

// ---------------------------------------------------------------------------
// VRAM layout (offsets into VRAM, LCDC = 0xF1)
//   0x0000  tile data, unsigned indexing (LCDC.4 = 1)
//   0x1800  BG tile map     (LCDC.3 = 0)
//   0x1C00  window tile map (LCDC.6 = 1)
// ---------------------------------------------------------------------------
constexpr std::uint16_t kBgMap  = 0x1800;
constexpr std::uint16_t kWndMap = 0x1C00;

constexpr std::uint8_t kLcdc = cppu::lcdc::kEnable | cppu::lcdc::kWndTileMap |
                               cppu::lcdc::kWndEnable | cppu::lcdc::kBgWndTileData |
                               cppu::lcdc::kObjEnable | cppu::lcdc::kBgWndEnable;

// Object palettes: OBP0 is the identity, OBP1 makes the ball lighter.
constexpr std::uint8_t kObp0 = 0xE4; // 1->1 2->2 3->3
constexpr std::uint8_t kObp1 = 0xD0; // 1->0 2->1 3->3

// Tile indices
constexpr std::uint8_t kTileGrass     = 0;
constexpr std::uint8_t kTileBrick     = 1;
constexpr std::uint8_t kTileBall      = 2;
constexpr std::uint8_t kTileWndBorder = 3;
constexpr std::uint8_t kTileWndBlank  = 4;
constexpr std::uint8_t kTileObjBall   = 5;
constexpr std::uint8_t kTileFontBase  = 16;

// The window slides between these WY values. WY = 144 is never reached by
// LY on a visible line, so the window is fully hidden there.
constexpr int kWyShown  = 120;
constexpr int kWyHidden = 144;

using TileArt = std::array<const char*, 8>;

// Tiles are drawn as 8 rows of 8 characters, each '0'-'3' (the 2-bit color).
const TileArt kGrass = {
    "00000000",
    "00000100",
    "00000000",
    "01000000",
    "00000000",
    "00000010",
    "00100000",
    "00000000",
};

const TileArt kBrick = {
    "00000000",
    "22202222",
    "22202222",
    "22202222",
    "00000000",
    "22222220",
    "22222220",
    "22222220",
};

const TileArt kBall = {
    "00333300",
    "03011230",
    "30112223",
    "31122223",
    "31222223",
    "32222223",
    "03222230",
    "00333300",
};

// Object tile: color 0 is transparent, so the corners show what's behind.
const TileArt kObjBall = {
    "00333300",
    "03111130",
    "31311313",
    "31111113",
    "31311313",
    "31133113",
    "03111130",
    "00333300",
};

const TileArt kWndBorder = {
    "33333333",
    "22222222",
    "11111111",
    "11111111",
    "11111111",
    "11111111",
    "11111111",
    "11111111",
};

const TileArt kWndBlank = {
    "11111111", "11111111", "11111111", "11111111",
    "11111111", "11111111", "11111111", "11111111",
};

// 5x7 font for the status bar. Each glyph is 7 rows of 5 bits (MSB = left).
struct Glyph {
    char ch;
    std::array<std::uint8_t, 7> rows;
};

const Glyph kFont[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
};
constexpr int kFontSize = static_cast<int>(sizeof(kFont) / sizeof(kFont[0]));

// Writes one tile's 16 bytes. Each row is a low byte (bit 0 of each color)
// followed by a high byte (bit 1), with the leftmost pixel in bit 7.
void write_tile(cppu::PPU& ppu, std::uint8_t index, const TileArt& art) {
    for (int row = 0; row < 8; ++row) {
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        for (int px = 0; px < 8; ++px) {
            const int color = art[static_cast<std::size_t>(row)][px] - '0';
            const int bit = 7 - px;
            lo = static_cast<std::uint8_t>(lo | ((color & 1) << bit));
            hi = static_cast<std::uint8_t>(hi | (((color >> 1) & 1) << bit));
        }
        const int addr = index * 16 + row * 2;
        ppu.write_vram(static_cast<std::uint16_t>(addr), lo);
        ppu.write_vram(static_cast<std::uint16_t>(addr + 1), hi);
    }
}

// Glyph pixels use color 3 on the window's color 1 background.
void write_glyph_tile(cppu::PPU& ppu, std::uint8_t index, const Glyph& g) {
    char rows[8][9];
    for (int row = 0; row < 8; ++row) {
        const std::uint8_t bits = row < 7 ? g.rows[static_cast<std::size_t>(row)] : 0;
        for (int px = 0; px < 8; ++px) {
            // Glyph columns 0-4 sit at tile columns 1-5.
            const int gx = px - 1;
            const bool on = gx >= 0 && gx < 5 && ((bits >> (4 - gx)) & 1);
            rows[row][px] = on ? '3' : '1';
        }
        rows[row][8] = '\0';
    }
    const TileArt art = {rows[0], rows[1], rows[2], rows[3],
                         rows[4], rows[5], rows[6], rows[7]};
    write_tile(ppu, index, art);
}

std::uint8_t glyph_tile(char ch) {
    for (int i = 0; i < kFontSize; ++i) {
        if (kFont[i].ch == ch) {
            return static_cast<std::uint8_t>(kTileFontBase + i);
        }
    }
    return kTileFontBase; // space
}

void write_map(cppu::PPU& ppu, std::uint16_t map, int x, int y, std::uint8_t tile) {
    ppu.write_vram(static_cast<std::uint16_t>(map + y * 32 + x), tile);
}

// BG: 4x4-tile blocks of grass and brick in a checkerboard, with balls on a
// regular grid. Every pattern repeats evenly across the 32x32 map, so it
// wraps seamlessly while scrolling.
std::uint8_t bg_tile_at(int x, int y) {
    if ((x % 8 == 2 && y % 8 == 5) || (x % 8 == 6 && y % 8 == 1)) {
        return kTileBall;
    }
    return ((x / 4 + y / 4) % 2) ? kTileBrick : kTileGrass;
}

} // namespace

Demo::Demo(cppu::PPU& ppu) : ppu_(ppu), wy_(kWyShown) {
    write_tile(ppu_, kTileGrass, kGrass);
    write_tile(ppu_, kTileBrick, kBrick);
    write_tile(ppu_, kTileBall, kBall);
    write_tile(ppu_, kTileWndBorder, kWndBorder);
    write_tile(ppu_, kTileWndBlank, kWndBlank);
    write_tile(ppu_, kTileObjBall, kObjBall);
    for (int i = 0; i < kFontSize; ++i) {
        write_glyph_tile(ppu_, static_cast<std::uint8_t>(kTileFontBase + i), kFont[i]);
    }

    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            write_map(ppu_, kBgMap, x, y, bg_tile_at(x, y));
        }
    }

    // Window: a border row, a text row and a blank row (24 px at WY = 120).
    for (int x = 0; x < 32; ++x) {
        write_map(ppu_, kWndMap, x, 0, kTileWndBorder);
        write_map(ppu_, kWndMap, x, 1, kTileWndBlank);
        write_map(ppu_, kWndMap, x, 2, kTileWndBlank);
    }
    write_status_text();

    ppu_.write_register(cppu::Reg::BGP, 0xE4); // identity: color n -> shade n
    ppu_.write_register(cppu::Reg::OBP0, kObp0);
    ppu_.write_register(cppu::Reg::OBP1, kObp1);

    // Balls with assorted speeds. Every other ball uses OBP1, and every third
    // has the BG-priority bit set, so it passes behind the bricks (BG colors
    // 1-3) but stays in front of the grass (color 0).
    for (int i = 0; i < kNumBalls; ++i) {
        Ball& b = balls_[static_cast<std::size_t>(i)];
        b.x  = 10.0 + i * 17.0;
        b.y  = 8.0 + i * 13.0;
        b.vx = (i % 2 ? -1.0 : 1.0) * (0.6 + 0.15 * i);
        b.vy = (i % 3 ? 1.0 : -1.0) * (0.9 - 0.07 * i);
        b.attrs = 0;
        if (i % 2) b.attrs |= cppu::oam::kPalette;
        if (i % 3 == 0) b.attrs |= cppu::oam::kBgPriority;
    }
    update_balls();
    ppu_.write_register(cppu::Reg::WX, 7);     // window starts at the left edge
    ppu_.write_register(cppu::Reg::WY, static_cast<std::uint8_t>(wy_));
    ppu_.write_register(cppu::Reg::LCDC, kLcdc);
}

void Demo::update(const Input& input) {
    if (input.toggle_auto_scroll) {
        auto_scroll_ = !auto_scroll_;
    }
    if (input.toggle_window) {
        window_shown_ = !window_shown_;
    }
    if (input.toggle_objects) {
        objects_shown_ = !objects_shown_;
    }

    if (auto_scroll_) {
        // Drift right while bobbing up and down.
        pos_x_ += 1.0;
        pos_y_ += 0.6 * std::cos(frame_ / 40.0);
    }
    pos_x_ += input.scroll_x;
    pos_y_ += input.scroll_y;

    // Slide the window in or out one line per frame by moving WY.
    const int wy_target = window_shown_ ? kWyShown : kWyHidden;
    if (wy_ < wy_target) ++wy_;
    if (wy_ > wy_target) --wy_;

    ppu_.write_register(cppu::Reg::SCX, static_cast<std::uint8_t>(static_cast<int>(std::floor(pos_x_)) & 0xFF));
    ppu_.write_register(cppu::Reg::SCY, static_cast<std::uint8_t>(static_cast<int>(std::floor(pos_y_)) & 0xFF));
    ppu_.write_register(cppu::Reg::WY, static_cast<std::uint8_t>(wy_));
    ppu_.write_register(cppu::Reg::LCDC, static_cast<std::uint8_t>(
        objects_shown_ ? kLcdc : (kLcdc & ~cppu::lcdc::kObjEnable)));
    write_status_text();
    update_balls();

    ++frame_;
}

// Moves each ball, bounces it off the screen edges, and writes it to OAM.
// OAM positions are offset: X = screen X + 8, Y = screen Y + 16.
void Demo::update_balls() {
    constexpr double kMaxX = cppu::kScreenWidth - 8;
    constexpr double kMaxY = cppu::kScreenHeight - 8;

    for (int i = 0; i < kNumBalls; ++i) {
        Ball& b = balls_[static_cast<std::size_t>(i)];
        b.x += b.vx;
        b.y += b.vy;
        if (b.x < 0)     { b.x = -b.x;            b.vx = -b.vx; }
        if (b.x > kMaxX) { b.x = 2 * kMaxX - b.x; b.vx = -b.vx; }
        if (b.y < 0)     { b.y = -b.y;            b.vy = -b.vy; }
        if (b.y > kMaxY) { b.y = 2 * kMaxY - b.y; b.vy = -b.vy; }

        const auto base = static_cast<std::uint8_t>(i * 4);
        ppu_.write_oam(base, static_cast<std::uint8_t>(static_cast<int>(b.y) + 16));
        ppu_.write_oam(static_cast<std::uint8_t>(base + 1), static_cast<std::uint8_t>(static_cast<int>(b.x) + 8));
        ppu_.write_oam(static_cast<std::uint8_t>(base + 2), kTileObjBall);
        ppu_.write_oam(static_cast<std::uint8_t>(base + 3), b.attrs);
    }
}

void Demo::write_status_text() {
    char text[21];
    std::snprintf(text, sizeof(text), "  SCX %03d   SCY %03d",
                  ppu_.read_register(cppu::Reg::SCX), ppu_.read_register(cppu::Reg::SCY));
    const int len = static_cast<int>(std::strlen(text));
    for (int x = 0; x < 20; ++x) {
        write_map(ppu_, kWndMap, x, 1, x < len ? glyph_tile(text[x]) : kTileWndBlank);
    }
}

} // namespace demo
