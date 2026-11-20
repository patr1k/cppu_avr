#pragma once

// Minimal stand-ins for the parts of the C++ standard library that cppu and
// the demo use. avr-gcc ships avr-libc (the C library) but no libstdc++, so
// <array>, <cstdint>, <utility>, <cmath>, ... don't exist on the AVR.
// Only included when building for __AVR__; desktop builds use the real thing.

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace std {

using ::int8_t;
using ::int16_t;
using ::int32_t;
using ::uint8_t;
using ::uint16_t;
using ::uint32_t;
using ::size_t;

using ::cos;
using ::floor;
using ::snprintf;
using ::strlen;

// Fixed-size array: an aggregate, so brace initialisation works like std::array.
template <class T, size_t N>
struct array {
    T elems_[N];

    constexpr T& operator[](size_t i) { return elems_[i]; }
    constexpr const T& operator[](size_t i) const { return elems_[i]; }
    constexpr T* data() { return elems_; }
    constexpr const T* data() const { return elems_; }
    static constexpr size_t size() { return N; }
    constexpr T* begin() { return elems_; }
    constexpr T* end() { return elems_ + N; }
    constexpr const T* begin() const { return elems_; }
    constexpr const T* end() const { return elems_ + N; }
};

template <class T, class U = T>
T exchange(T& obj, U new_value) {
    T old = obj;
    obj = static_cast<T>(new_value);
    return old;
}

} // namespace std
