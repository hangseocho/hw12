// bench_u64toa.cpp — cycles/op and ns/op for u64toa vs snprintf, std::to_string,
// std::to_chars and the staged versions, over a range of magnitudes.
//
//   ./bench_u64toa [runs]      (default 31)
//
// For each magnitude (values with exactly 1, 2, 4, 8, 12, 16, 20 digits, plus "mixed":
// uniform over bit length 1..64) a pass converts 4096 random values into consecutive
// 32-byte slots of an output buffer, like serializing a batch. Every method gets
// warm-up passes, then `runs` timed passes, interleaved round-robin (pass r of
// every method, then pass r+1), and the median pass is reported per op.
//
// Cycles: x86 -> __rdtsc() (TSC reference cycles). Elsewhere (Apple Silicon has no
// user-readable cycle counter) -> ns x core GHz, with the core clock measured right
// before the run by a chain of dependent register-register adds (1 add = 1 cycle).
#include "u64toa.hpp"
#include "u64toa_variants.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <initializer_list>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define HAVE_RDTSC 1
#else
#define HAVE_RDTSC 0
#endif

using Clock = std::chrono::steady_clock;
static inline std::uint64_t ticks() {
#if HAVE_RDTSC
    return __rdtsc();
#else
    return 0;
#endif
}

static double measure_core_ghz() {
    double best = 0;
    for (int rep = 0; rep < 7; ++rep) {
        const long iters = 20000000;
        std::uint64_t x = 0, one = 1;
        const auto t0 = Clock::now();
        for (long i = 0; i < iters; ++i) {
#if defined(__aarch64__)
            asm volatile("add %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1\n\t"
                         "add %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1\n\tadd %0, %0, %1"
                         : "+r"(x) : "r"(one));
#elif defined(__x86_64__)
            asm volatile("add %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\t"
                         "add %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0\n\tadd %1, %0"
                         : "+r"(x) : "r"(one));
#else
            return 0;
#endif
        }
        const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count();
        best = std::max(best, (double)iters * 10.0 / ns);
        if (x == 42) std::puts("");
    }
    return (best > 0.5 && best < 6.5) ? best : 0;
}

// ---------------------------------------------------------------- methods
struct Snprintf { static int conv(std::uint64_t v, char* p) { return std::snprintf(p, 32, "%llu", (unsigned long long)v); } };
struct ToString {
    static int conv(std::uint64_t v, char* p) {
        const std::string s = std::to_string(v);  // the string is the cost; copy out like a serializer would
        std::memcpy(p, s.data(), s.size());
        return (int)s.size();
    }
};
struct ToChars { static int conv(std::uint64_t v, char* p) { return (int)(std::to_chars(p, p + 32, v).ptr - p); } };
struct V1 { static int conv(std::uint64_t v, char* p) { return v1_naive(v, p); } };
struct V2 { static int conv(std::uint64_t v, char* p) { return v2_backwards(v, p); } };
struct V3 { static int conv(std::uint64_t v, char* p) { return v3_pairs(v, p); } };
struct Final { static int conv(std::uint64_t v, char* p) { return u64toa(v, p); } };
struct AltMemcpy { static int conv(std::uint64_t v, char* p) { return alt_memcpy(v, p); } };

constexpr int kN = 4096;
static char g_out[kN * 32];

template <class M>
static std::uint64_t pass(const std::uint64_t* vals) {
    std::uint64_t total = 0;
    char* p = g_out;
    for (int i = 0; i < kN; ++i, p += 32) total += (std::uint64_t)M::conv(vals[i], p);
    asm volatile("" : : "r"(g_out) : "memory");  // the written text is "used"
    return total + (std::uint64_t)g_out[0];
}

using PassFn = std::uint64_t (*)(const std::uint64_t*);
static const PassFn kPasses[] = {pass<Snprintf>, pass<ToString>, pass<ToChars>, pass<V1>, pass<V2>, pass<V3>, pass<Final>, pass<AltMemcpy>};
static const char* kNames[] = {"snprintf(\"%llu\")", "std::to_string", "std::to_chars (C++17)",
                               "v1: digit loop + reverse", "v2: count digits, write backwards",
                               "v3: + 2-digit table", "FINAL u64toa (v3 + 8-digit chunks)",
                               "  alt: FINAL with 2-byte memcpy pairs"};
constexpr int kM = 8;
constexpr int kFinal = 6;

static volatile std::uint64_t g_sink;

int main(int argc, char** argv) {
    const int runs = argc > 1 ? std::atoi(argv[1]) : 31;
    const double ghz = measure_core_ghz();

    struct Mag { const char* name; int digits; };  // digits 0 = mixed
    const Mag mags[] = {{"1", 1}, {"2", 2}, {"4", 4}, {"8", 8}, {"12", 12}, {"16", 16}, {"20", 20}, {"mixed", 0}};
    constexpr int kMags = 8;

    std::mt19937_64 rng(2026);
    double cyc[kM][kMags], nsop[kM][kMags];
    std::vector<std::uint64_t> vals(kN);
    for (int g = 0; g < kMags; ++g) {
        const int d = mags[g].digits;
        for (auto& v : vals) {
            if (d == 0) {
                const int bits = 1 + (int)(rng() % 64);
                v = bits == 64 ? rng() : rng() & ((std::uint64_t(1) << bits) - 1);
            } else {
                std::uint64_t lo = 1;
                for (int i = 1; i < d; ++i) lo *= 10;  // 10^(d-1)
                const std::uint64_t span = (d == 20) ? (UINT64_MAX - lo) : (lo * 10 - lo);
                v = (d == 1) ? rng() % 10 : lo + rng() % span;
            }
        }
        for (int w = 0; w < 5; ++w)
            for (int m = 0; m < kM; ++m) g_sink = g_sink + kPasses[m](vals.data());
        std::vector<double> ns[kM], tk[kM];
        for (int r = 0; r < runs; ++r)
            for (int m = 0; m < kM; ++m) {
                const auto t0 = Clock::now();
                const std::uint64_t c0 = ticks();
                const std::uint64_t s = kPasses[m](vals.data());
                const std::uint64_t c1 = ticks();
                ns[m].push_back(std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / kN);
                tk[m].push_back((double)(c1 - c0) / kN);
                g_sink = g_sink + s;
            }
        for (int m = 0; m < kM; ++m) {
            std::sort(ns[m].begin(), ns[m].end());
            std::sort(tk[m].begin(), tk[m].end());
            nsop[m][g] = ns[m][runs / 2];
            cyc[m][g] = HAVE_RDTSC ? tk[m][runs / 2] : nsop[m][g] * ghz;
        }
    }

    std::printf("u64toa benchmark: %d values per pass, %d runs, median per op\n", kN, runs);
    std::printf("cycles: %s; measured core clock: %s\n\n",
                HAVE_RDTSC ? "__rdtsc() (TSC reference cycles)" : "ns x measured core GHz",
                ghz > 0 ? (std::to_string(ghz).substr(0, 4) + " GHz").c_str() : "n/a");
    auto table = [&](const char* title, double (*t)[kMags]) {
        std::printf("%s (digits ->)\n%-38s", title, "method");
        for (const Mag& mg : mags) std::printf("%8s", mg.name);
        std::printf("\n");
        for (int m = 0; m < kM; ++m) {
            std::printf("%-38s", kNames[m]);
            for (int g = 0; g < kMags; ++g) std::printf("%8.1f", t[m][g]);
            std::printf("\n");
        }
        std::printf("\n");
    };
    table("cycles/op", cyc);
    table("ns/op", nsop);

    std::printf("speedup of FINAL (cycles, higher is better)\n%-38s", "vs");
    for (const Mag& mg : mags) std::printf("%8s", mg.name);
    std::printf("\n");
    for (int m : {0, 1, 2, 3, 7}) {
        std::printf("%-38s", kNames[m]);
        for (int g = 0; g < kMags; ++g) std::printf("%7.1fx", cyc[m][g] / cyc[kFinal][g]);
        std::printf("\n");
    }
    return 0;
}
