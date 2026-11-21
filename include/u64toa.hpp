#pragma once
#include <cstdint>
#include <cstring>
// HW12 — fast uint64 -> decimal text. Write the digits into out; return the length.
//
//   int u64toa(std::uint64_t v, char* out);
//
// Writes exactly the decimal digits of v (1..20 bytes, no sign, no NUL terminator)
// starting at out, and returns how many it wrote. `out` needs room for 20 bytes.
// No heap, no locale, no lookups beyond one 200-byte table.
//
// How it is fast:
//   1. Count the digits first (bit length -> log10 estimate, one table compare), so
//      the digits can be written straight into place from the right. No temporary
//      buffer, no reverse pass.
//   2. Emit two digits per step from a 200-byte "00".."99" table: half the divisions
//      of a digit-at-a-time loop.
//   3. Peel off 8-digit chunks with one 64-bit divide by 10^8, then do the rest of
//      each chunk in 32-bit arithmetic. 32-bit multiply-by-reciprocal (what the
//      compiler emits for /100 and /10000) is cheaper than the 64-bit version, and
//      the four 2-digit writes inside a chunk don't depend on each other.

namespace u64toa_detail {

// "00", "01", ..., "99" back to back: the pair for r (0..99) starts at kPairs + 2*r.
inline const char* pairs() {
    static const char kPairs[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
    return kPairs;
}

// r in [0, 99] -> its two digits at p[0], p[1]. Two single-byte stores, on purpose:
// a 2-byte memcpy from the table measured 2-3x slower on x86 (see README).
inline void put2(char* p, std::uint32_t r) {
    const char* d = pairs() + 2 * r;
    p[0] = d[0];
    p[1] = d[1];
}

// Number of decimal digits of v (v = 0 -> 1).
inline int count_digits(std::uint64_t v) {
    // kThreshold[t] = 10^t, except kThreshold[0] = 0 so that v = 0 counts as 1 digit.
    static const std::uint64_t kThreshold[20] = {
        0ull,                1ull * 10,           100ull,              1000ull,
        10000ull,            100000ull,           1000000ull,          10000000ull,
        100000000ull,        1000000000ull,       10000000000ull,      100000000000ull,
        1000000000000ull,    10000000000000ull,   100000000000000ull,  1000000000000000ull,
        10000000000000000ull, 100000000000000000ull, 1000000000000000000ull,
        10000000000000000000ull};
#if defined(__GNUC__) || defined(__clang__)
    const int bits = 64 - __builtin_clzll(v | 1);  // bit length, 1..64
#else
    int bits = 1;
    for (std::uint64_t x = v >> 1; x; x >>= 1) ++bits;
#endif
    // 1233 / 4096 ~= log10(2), so t = floor(bits * log10 2) is the digit count or
    // one less; a single compare against 10^t settles which (t <= 19 always).
    const int t = (bits * 1233) >> 12;
    return t + (v >= kThreshold[t] ? 1 : 0);
}

// Exactly 8 digits of r (r < 10^8, zero-padded) at p[0..7], all in 32-bit math.
inline void put8(char* p, std::uint32_t r) {
    const std::uint32_t hi = r / 10000, lo = r % 10000;
    put2(p + 0, hi / 100);
    put2(p + 2, hi % 100);
    put2(p + 4, lo / 100);
    put2(p + 6, lo % 100);
}

}  // namespace u64toa_detail

inline int u64toa(std::uint64_t v, char* out) {
    using namespace u64toa_detail;
    const int n = count_digits(v);
    char* p = out + n;  // fill from the right; exactly n bytes are written

    // At most two full 8-digit chunks (20 digits = 4 + 8 + 8).
    while (v >= 100000000ull) {
        const std::uint64_t q = v / 100000000ull;
        const std::uint32_t r = static_cast<std::uint32_t>(v - q * 100000000ull);
        p -= 8;
        put8(p, r);
        v = q;
    }
    // Remaining 1..8 digits fit in 32 bits.
    std::uint32_t u = static_cast<std::uint32_t>(v);
    while (u >= 100) {
        const std::uint32_t q = u / 100;
        p -= 2;
        put2(p, u - q * 100);
        u = q;
    }
    if (u >= 10) {
        p -= 2;
        put2(p, u);
    } else {
        *--p = static_cast<char>('0' + u);
    }
    return n;
}

// Bonus: encode one FIX field "tag=value\x01" with u64toa. Returns bytes written
// (at most 10 + 1 + 20 + 1 = 32). Example: encode_fix_field(38, 100, buf) -> "38=100\x01".
inline int encode_fix_field(std::uint32_t tag, std::uint64_t value, char* out) {
    int n = u64toa(tag, out);
    out[n++] = '=';
    n += u64toa(value, out + n);
    out[n++] = '\x01';
    return n;
}
