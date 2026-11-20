#pragma once

// avrboy: decoding one 8-pixel tile row (two bitplanes) into raw colors.

#if defined(__AVR__)
#include "../avr_std.hpp"
#else
#include <cstdint>
#endif

namespace cppu {

// Reference version: out[i] = color (0-3) of pixel i, leftmost (bit 7) first.
inline void decode_tile_row_ref(std::uint8_t* out, std::uint8_t lo, std::uint8_t hi) {
    for (int i = 7; i >= 0; --i) {
        *out++ = static_cast<std::uint8_t>((((hi >> i) & 1) << 1) | ((lo >> i) & 1));
    }
}

#if defined(__AVR__)
// AVR version, 7 cycles per pixel: shift each plane's top bit through carry
// into the color. (avr-gcc compiles the reference version into 16-bit shift
// chains, about 3x slower.) Checked against decode_tile_row_ref for all
// 65536 inputs by the ppu_demo startup self-test.
//
// The pixel is written out 8 times rather than with .rept: GCC sizes inline
// asm by counting lines, and an undercount breaks branches around it
// ("relocation truncated to fit").
inline void decode_tile_row(std::uint8_t* out, std::uint8_t lo, std::uint8_t hi) {
    std::uint8_t t;
#define CPPU_DECODE_PX \
        "clr  %[t]            \n\t" \
        "lsl  %[hi]           \n\t" \
        "rol  %[t]            \n\t" \
        "lsl  %[lo]           \n\t" \
        "rol  %[t]            \n\t" \
        "st   %a[out]+, %[t]  \n\t"
    asm volatile(
        CPPU_DECODE_PX CPPU_DECODE_PX CPPU_DECODE_PX CPPU_DECODE_PX
        CPPU_DECODE_PX CPPU_DECODE_PX CPPU_DECODE_PX CPPU_DECODE_PX
        : [out] "+e"(out), [lo] "+r"(lo), [hi] "+r"(hi), [t] "=&r"(t)
        :
        : "memory");
#undef CPPU_DECODE_PX
}
#else
inline void decode_tile_row(std::uint8_t* out, std::uint8_t lo, std::uint8_t hi) {
    decode_tile_row_ref(out, lo, hi);
}
#endif

} // namespace cppu
