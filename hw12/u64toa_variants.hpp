// u64toa_variants.hpp — the intermediate versions used to measure "before vs after".
// Not part of the CI interface; ../include/u64toa.hpp is the submission (= v4).
//
//   v1_naive      digit-at-a-time into a temp buffer (v % 10, v /= 10), then reverse
//   v2_backwards  count digits first, write right-to-left in place: no temp, no reverse
//   v3_pairs      + two digits per step from the "00".."99" table (64-bit / 100)
//   v4 = u64toa   + 8-digit chunks: one 64-bit / 10^8, the rest in 32-bit math
//   alt_memcpy    v4, but each pair written with one 2-byte memcpy instead of two byte stores
#pragma once

#include "u64toa.hpp"

#include <cstdint>
#include <cstring>

inline int v1_naive(std::uint64_t v, char* out) {
    char tmp[20];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return n;
}

inline int v2_backwards(std::uint64_t v, char* out) {
    const int n = u64toa_detail::count_digits(v);
    char* p = out + n;
    do {
        *--p = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0);
    return n;
}

inline int v3_pairs(std::uint64_t v, char* out) {
    const int n = u64toa_detail::count_digits(v);
    char* p = out + n;
    while (v >= 100) {
        const std::uint64_t q = v / 100;
        p -= 2;
        u64toa_detail::put2(p, static_cast<std::uint32_t>(v - q * 100));
        v = q;
    }
    if (v >= 10) { p -= 2; u64toa_detail::put2(p, static_cast<std::uint32_t>(v)); }
    else *--p = static_cast<char>('0' + v);
    return n;
}

// The final algorithm with the pair written as a single 2-byte memcpy from the table.
inline int alt_memcpy(std::uint64_t v, char* out) {
    using namespace u64toa_detail;
    auto put2m = [](char* p, std::uint32_t r) { std::memcpy(p, pairs() + 2 * r, 2); };
    const int n = count_digits(v);
    char* p = out + n;
    while (v >= 100000000ull) {
        const std::uint64_t q = v / 100000000ull;
        const std::uint32_t r = static_cast<std::uint32_t>(v - q * 100000000ull);
        const std::uint32_t hi = r / 10000, lo = r % 10000;
        p -= 8;
        put2m(p, hi / 100); put2m(p + 2, hi % 100); put2m(p + 4, lo / 100); put2m(p + 6, lo % 100);
        v = q;
    }
    std::uint32_t u = static_cast<std::uint32_t>(v);
    while (u >= 100) { const std::uint32_t q = u / 100; p -= 2; put2m(p, u - q * 100); u = q; }
    if (u >= 10) { p -= 2; put2m(p, u); } else { *--p = static_cast<char>('0' + u); }
    return n;
}
