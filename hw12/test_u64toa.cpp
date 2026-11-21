// test_u64toa.cpp — correctness tests for ../include/u64toa.hpp (and the staged variants).
//   ./test_u64toa        exits non-zero on the first failure
#include "u64toa.hpp"
#include "u64toa_variants.hpp"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

static long g_checks = 0;  // CHECK()s evaluated
static long g_values = 0;  // values verified (each against all 5 versions)
#define CHECK(cond)                                                                       \
    do {                                                                                  \
        ++g_checks;                                                                       \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "CHECK failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

// Count heap allocations, to show u64toa makes none.
static long g_allocs = 0;
void* operator new(std::size_t n) {
    ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using Fn = int (*)(std::uint64_t, char*);
static const Fn kFns[] = {u64toa, v1_naive, v2_backwards, v3_pairs, alt_memcpy};
static const char* kNames[] = {"u64toa", "v1_naive", "v2_backwards", "v3_pairs", "alt_memcpy"};

// Reference: std::to_chars (exact, locale-free). Checks digits, length, and that
// not a single byte outside [out, out+n) is touched (canaries on both sides).
static void check_one(std::uint64_t v) {
    char ref[32];
    const auto rc = std::to_chars(ref, ref + sizeof ref, v);
    const int want = static_cast<int>(rc.ptr - ref);
    for (int f = 0; f < 5; ++f) {
        char buf[48];
        std::memset(buf, '#', sizeof buf);
        const int n = kFns[f](v, buf + 8);
        if (n != want || std::memcmp(buf + 8, ref, (size_t)want) != 0) {
            std::fprintf(stderr, "%s(%llu) wrong: got \"%.*s\" (%d)\n", kNames[f], (unsigned long long)v,
                         n > 0 && n < 40 ? n : 0, buf + 8, n);
            std::exit(1);
        }
        bool canaries_ok = true;
        for (int i = 0; i < 8; ++i) canaries_ok &= (buf[i] == '#');                     // nothing before out
        for (int i = 8 + n; i < (int)sizeof buf; ++i) canaries_ok &= (buf[i] == '#');  // nothing past out+n
        CHECK(canaries_ok);
    }
    ++g_values;
}

static void test_interface() {
    static_assert(std::is_same<decltype(&::u64toa), int (*)(std::uint64_t, char*)>::value, "signature");
    std::puts("[ok] interface: int u64toa(std::uint64_t, char*)");
}

static void test_course_cases() {
    // The exact values the CI test uses, compared with std::to_string like CI does.
    const std::uint64_t vs[] = {0, 7, 12345, 1000000, 9999999999ULL, UINT64_MAX};
    for (std::uint64_t v : vs) {
        char buf[32];
        const int n = u64toa(v, buf);
        buf[n] = 0;
        CHECK(std::to_string(v) == buf);
    }
    std::uint64_t x = 88172645463325252ULL;  // the CI's xorshift sequence
    for (int i = 0; i < 200000; ++i) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        char buf[32];
        const int n = u64toa(x, buf);
        buf[n] = 0;
        CHECK(std::to_string(x) == buf);
    }
    std::puts("[ok] course CI cases: 0, 7, 12345, 1000000, 9999999999, UINT64_MAX, 200k xorshift");
}

static void test_edges() {
    char z[32];
    CHECK(u64toa(0, z) == 1 && z[0] == '0');
    for (std::uint64_t v = 0; v <= 9; ++v) check_one(v);
    check_one(UINT64_MAX);
    check_one(UINT64_MAX - 1);
    check_one(std::numeric_limits<std::uint32_t>::max());
    check_one(std::uint64_t(std::numeric_limits<std::uint32_t>::max()) + 1);
    // Every digit-count boundary: 10^k - 1, 10^k, 10^k + 1 (k = 1..19)
    std::uint64_t p = 1;
    for (int k = 1; k <= 19; ++k) {
        p *= 10;
        check_one(p - 1); check_one(p); check_one(p + 1);
    }
    // Every bit-length boundary: 2^k - 1, 2^k, 2^k + 1 (count_digits uses the bit length)
    for (int k = 1; k < 64; ++k) {
        const std::uint64_t b = std::uint64_t(1) << k;
        check_one(b - 1); check_one(b); check_one(b + 1);
    }
    // Chunk boundaries of the 8-digit split: 10^8 and 10^16 neighborhoods, zeros inside chunks
    const std::uint64_t specials[] = {100000000ull, 99999999ull, 100000001ull, 10000000000000000ull,
                                      9999999999999999ull, 10000000000000001ull, 10000000100000001ull,
                                      12345678000000009ull, 18000000000000000000ull, 10000000000000000000ull};
    for (std::uint64_t v : specials) check_one(v);
    std::puts("[ok] edges: 0..9, UINT64_MAX(-1), 2^32 boundary, all 10^k+-1, all 2^k+-1, chunk boundaries");
}

static void test_exhaustive_low() {
    for (std::uint64_t v = 0; v < 10000000; ++v) check_one(v);  // every value with <= 7 digits
    std::puts("[ok] exhaustive 0 .. 9,999,999 (all 5 versions, canaries)");
}

static void test_random_magnitudes() {
    std::mt19937_64 rng(12);
    for (int i = 0; i < 5000000; ++i) {
        const int bits = 1 + (int)(rng() % 64);  // uniform over bit lengths, so small and large values both appear
        const std::uint64_t v = bits == 64 ? rng() : rng() & ((std::uint64_t(1) << bits) - 1);
        check_one(v);
    }
    std::puts("[ok] 5,000,000 random values, uniform over bit length 1..64");
}

static void test_no_allocation() {
    char buf[32];
    long sink = 0;
    const long before = g_allocs;
    for (std::uint64_t v = 0; v < 1000000; ++v) sink += u64toa(v * 7919, buf) + buf[0];
    CHECK(g_allocs == before && sink > 0);
    std::puts("[ok] zero heap allocations over 1,000,000 calls");
}

static void test_encode_field() {
    char buf[64];
    int n = encode_fix_field(38, 100, buf);
    CHECK(std::string(buf, (size_t)n) == "38=100\x01");
    n = encode_fix_field(11, 0, buf);
    CHECK(std::string(buf, (size_t)n) == "11=0\x01");
    n = encode_fix_field(4294967295u, UINT64_MAX, buf);
    CHECK(n == 32 && std::string(buf, (size_t)n) == "4294967295=18446744073709551615\x01");
    // a whole message body built from fields
    std::string msg;
    const std::pair<std::uint32_t, std::uint64_t> fields[] = {{34, 1}, {38, 100}, {54, 1}};
    for (auto& f : fields) { n = encode_fix_field(f.first, f.second, buf); msg.append(buf, (size_t)n); }
    CHECK(msg == "34=1\x01" "38=100\x01" "54=1\x01");
    std::puts("[ok] bonus encode_fix_field: \"tag=value\\x01\", max 32 bytes");
}

int main() {
    test_interface();
    test_course_cases();
    test_edges();
    test_encode_field();
    test_no_allocation();
    test_exhaustive_low();
    test_random_magnitudes();
    std::printf("ALL TESTS PASSED (%ld values x 5 versions, %ld checks)\n", g_values, g_checks);
    return 0;
}
