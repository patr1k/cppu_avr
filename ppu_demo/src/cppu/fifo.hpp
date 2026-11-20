#pragma once

#if defined(__AVR__)
#include "../avr_std.hpp"
#else
#include <array>
#include <cstdint>
#endif

namespace cppu {

// One pixel waiting in a pixel FIFO. Colors are raw 2-bit tile colors (0-3);
// palettes are applied when the pixel is output.
struct Pixel {
    std::uint8_t color      = 0;     // 0-3; for objects, 0 = transparent
    bool palette            = false; // objects only: false = OBP0, true = OBP1
    bool bg_priority        = false; // objects only: hide behind BG colors 1-3
};

// Fixed-size ring buffer of pixels. Holds up to 16, which is enough for the
// 8 pixels of a BG tile row plus the 8 pending pixels of an object row.
class PixelFifo {
public:
    void push(Pixel p) {
        buf_[(head_ + size_++) & kMask] = p;
    }

    Pixel pop() {
        Pixel p = buf_[head_];
        head_ = (head_ + 1) & kMask;
        --size_;
        return p;
    }

    // The i-th pixel from the front, for merging object pixels in place.
    // No bounds check: i must be < size().
    Pixel& at(std::uint8_t i) {
        return buf_[(head_ + i) & kMask];
    }

    std::uint8_t size() const { return size_; }

    bool empty() const { return size_ == 0; }

    void clear() {
        head_ = 0;
        size_ = 0;
    }

private:
    static constexpr std::uint8_t kCapacity = 16;
    static constexpr std::uint8_t kMask = kCapacity - 1;
    std::array<Pixel, kCapacity> buf_{};
    std::uint8_t head_ = 0;
    std::uint8_t size_ = 0;
};

} // namespace cppu
