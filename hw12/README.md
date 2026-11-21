# Week 12 — Performance Challenge: `u64toa`

**Hang Seo Cho**

`u64toa` (in `../include/u64toa.hpp`) converts a `uint64_t` to decimal text with no heap, no locale and
no format parsing. On my Mac (Apple Silicon) it is:
- **4.6–19.9× faster than `snprintf`**, across magnitudes;
- **1.8–4.8× faster than `std::to_string`**;
- up to **2.9× faster than my first correct version** (1.9× on mixed input). Only at 1 digit is it
  0.2 ns slower, where everything is under 2 ns.

**Course autograder:** I ran `python3 tests/run_ci.py` with this header. Result: **u64toa edge cases
8/8, fuzz vs std 12/12**. The CI metric was `u64toa_ns_per_op` = 4.6 vs `std_to_string_ns_per_op` = 25.8
on my Linux VM.

| File | Purpose |
|---|---|
| `../include/u64toa.hpp` | **The submission**, graded by CI at this path. Also has the bonus `encode_fix_field`. |
| `u64toa_variants.hpp` | The intermediate versions v1–v3 and an "alt" form, used for the measurements |
| `test_u64toa.cpp` | Correctness tests (15 M values × 5 versions) |
| `bench_u64toa.cpp` | Benchmark: cycles/op and ns/op over magnitudes vs `snprintf` / `std::to_string` / `std::to_chars` |
| `bench_results_mac.txt`, `bench_results_linux.txt` | Raw benchmark output |
| `Makefile` | `make test`, `make asan`, `make bench`. Run inside `hw12/`. |

## Interface

```cpp
inline int u64toa(std::uint64_t v, char* out);   // writes the digits, returns the length (1..20)
inline int encode_fix_field(std::uint32_t tag, std::uint64_t value, char* out);  // bonus: "tag=value\x01"
```

`u64toa` writes **exactly** `n` bytes: no NUL terminator and no scratch bytes past the end, so it can
write straight into a message being built. `out` needs room for 20 bytes. `encode_fix_field` writes at
most 32 bytes. For example, `encode_fix_field(38, 100, buf)` writes `"38=100\x01"`.

## How it works

1. **Count the digits first.** Take the bit length from `clz`, multiply by `1233/4096 ≈ log10 2`, and do
   one table compare. That gives the exact digit count `n`. So the digits can be written right-to-left
   straight into `out[n-1] … out[0]`: no temporary buffer, no reverse pass.
2. **Two digits per step.** Look up `v % 100` in a 200-byte table `"00" "01" … "99"`. This halves the
   number of divisions. The compiler turns each `/100` into a multiply by a reciprocal.
3. **8-digit chunks.** One 64-bit `/ 10^8` splits off the low 8 digits. The rest of each chunk uses
   32-bit `/10000`, `/100` and `%100`. These are cheaper, and the four pair lookups inside a chunk don't
   depend on each other, so they can run in parallel.
4. **Store each pair as two single bytes,** not as one 2-byte `memcpy`. See the measurements below for why.

## Correctness

All of the following pass under `make test`. They also pass under ASan + UBSan (`make asan`) with
GCC 13.3 and Clang 18.1 on Linux, and `make test` passes on the Mac.

- **The exact CI cases:** `0, 7, 12345, 1000000, 9999999999, UINT64_MAX`, plus the CI's 200k xorshift
  values, all compared with `std::to_string`.
- **Edge cases:**
  - 0–9, `UINT64_MAX` and `UINT64_MAX - 1`, and the 2³² boundary
  - every digit-count boundary 10ᵏ−1 / 10ᵏ / 10ᵏ+1
  - every bit-length boundary 2ᵏ−1 / 2ᵏ / 2ᵏ+1 (these test the `clz` digit count)
  - the 8-digit chunk boundaries, including zeros inside a chunk
- **Exhaustive:** every value from 0 to 9,999,999, plus 5,000,000 random values spread evenly over
  bit length 1–64.
- **How each value is checked:** against `std::to_chars`, for all 5 versions. Canary bytes on both
  sides of the output must stay untouched, which proves exactly `n` bytes were written.
- **Zero heap allocations:** counted over 1,000,000 calls with a counting global `operator new`.
- **Bonus `encode_fix_field`:** checked on normal fields, `0`, and the 32-byte maximum.
- **Interface:** checked by `static_assert`; warning-free with `-Wall -Wextra -Wpedantic -Wconversion
  -Wsign-conversion -Wshadow -Werror` under C++14/17/20.

## Measurements

**Method.**
- **Workload:** for each magnitude (values with exactly 1, 2, 4, 8, 12, 16 or 20 digits, plus
  **mixed**, which is spread evenly over bit length 1–64), one pass converts 4,096 random values into
  consecutive 32-byte slots, like serializing a batch.
- **Timing:** each method gets 5 warm-up passes, then 31 timed passes. Methods are interleaved
  round-robin (pass *r* of every method, then pass *r+1*), and the median is reported.
- **Cycles:** on x86 from `__rdtsc()`. On Apple Silicon, where user code can't read a cycle counter,
  cycles = ns × core clock. The core clock (3.42 GHz) is measured just before the run with a chain of
  dependent register adds, which runs at exactly one add per cycle.
- **Mac** (primary): Apple Silicon, Apple clang, libc++, `-O2`.
- **Linux** (secondary): x86-64 Xeon VM, 2 vCPUs, GCC 13.3, libstdc++, `-O2`.

### Mac: cycles/op

| Method | 1 digit | 2 | 4 | 8 | 12 | 16 | 20 | mixed |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `snprintf("%llu")` | 107.7 | 110.7 | 122.9 | 120.8 | 130.9 | 133.8 | 151.0 | 172.2 |
| `std::to_string` | 22.2 | 23.5 | 35.3 | 27.2 | 40.3 | 36.3 | 51.6 | 68.2 |
| `std::to_chars` (reference) | 4.3 | 5.1 | 5.4 | 7.5 | 11.0 | 13.4 | 16.2 | 36.3 |
| v1: digit loop + reverse | 4.6 | 7.1 | 15.1 | 17.8 | 30.0 | 38.1 | 54.1 | 70.6 |
| v2: count digits, write backwards | 5.0 | 5.3 | 9.7 | 16.8 | 25.7 | 35.6 | 46.8 | 56.3 |
| v3: + 2-digit table | 4.2 | 3.7 | 6.7 | 10.5 | 15.1 | 20.2 | 25.5 | 42.5 |
| **FINAL: + 8-digit chunks** | **5.4** | **6.1** | **7.3** | **11.1** | **13.8** | **16.9** | **18.8** | **37.6** |
| *alt: FINAL, 2-byte `memcpy` pairs* | *5.4* | *6.1* | *7.1* | *12.1* | *13.3* | *16.4* | *18.3* | *37.7* |
| **FINAL vs `snprintf`** | **19.9×** | 18.2× | 16.8× | 10.9× | 9.5× | 7.9× | 8.0× | **4.6×** |
| **FINAL vs `std::to_string`** | **4.1×** | 3.9× | 4.8× | 2.5× | 2.9× | 2.1× | 2.7× | **1.8×** |
| **FINAL vs v1 (before → after)** | 0.8× | 1.2× | 2.1× | 1.6× | 2.2× | 2.3× | **2.9×** | **1.9×** |

In ns/op on the Mac, FINAL takes 1.6–5.5 ns at fixed magnitudes and 11.0 ns on mixed. `snprintf` takes
31–50 ns and `std::to_string` 6.5–20 ns. The full tables are in `bench_results_mac.txt`.

### Linux x86-64: cycles/op (TSC)

| Method | 1 digit | 2 | 4 | 8 | 12 | 16 | 20 | mixed |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `snprintf("%llu")` | 64.6 | 61.4 | 66.9 | 71.2 | 76.0 | 86.0 | 143.3 | 147.2 |
| `std::to_string` | 16.8 | 24.8 | 25.2 | 29.6 | 33.9 | 61.9 | 103.4 | 85.5 |
| `std::to_chars` (reference) | 7.2 | 7.1 | 9.6 | 17.2 | 23.6 | 30.5 | 52.6 | 58.5 |
| v1: digit loop + reverse | 3.6 | 5.8 | 10.3 | 21.0 | 31.2 | 41.4 | 76.3 | 64.5 |
| v2: count digits, write backwards | 3.8 | 5.5 | 8.4 | 15.3 | 23.3 | 32.1 | 54.6 | 50.5 |
| v3: + 2-digit table | 3.0 | 3.4 | 5.0 | 8.6 | 12.6 | 17.0 | 28.5 | 40.4 |
| **FINAL: + 8-digit chunks** | **3.5** | **3.8** | **5.4** | **8.5** | **14.7** | **17.7** | **30.6** | **44.1** |
| *alt: FINAL, 2-byte `memcpy` pairs* | *6.2* | *11.0* | *17.4* | *30.1* | *39.2* | *53.2* | *87.6* | *84.6* |

On Linux, FINAL is **3.3–18.5× faster than `snprintf`**, **1.9–6.5× faster than `std::to_string`**, and
**1.3–2.1× faster than libstdc++'s `std::to_chars`**.

### The single change that helped most: the 2-digit table (v2 → v3)

The biggest step from the first correct version was emitting **two digits per step from the
`"00".."99"` table**:
- on the Mac, 46.8 → 25.5 cycles at 20 digits (**1.84×**) and 56.3 → 42.5 on mixed;
- on Linux, 54.6 → 28.5 (**1.92×**).

A digit-at-a-time loop does one dependent divide (a multiply-by-reciprocal plus a shift) per digit,
so a 20-digit number is a chain of 20 of them. The table halves the length of that chain, and each
table lookup is a cheap L1 load that runs alongside the arithmetic. For comparison, counting digits
first (v1 → v2) was worth about 1.25× on mixed input (up to 1.56× at some magnitudes), and the 8-digit chunks another 1.13× on mixed and 1.36× at 20 digits on
the Mac.

How each pair is stored turned out to matter as much as the idea itself. My first version wrote each
pair with one 2-byte `memcpy` from the table. On the x86 machine that made the table version *slower*
than the plain digit loop: the "alt" row is 1.8–3.5× slower than FINAL. On the Mac the two forms are
identical. I didn't pin down the exact micro-architectural cause on that Xeon. But the measurement was
consistent in both throughput and latency tests, and two byte stores are never worse on either
machine, so FINAL uses them.

### Notes

- **`std::to_chars` on the Mac.** libc++'s `std::to_chars` is faster than FINAL at fixed magnitudes:
  FINAL takes 1.16–1.48× its cycles. On mixed input the two are nearly equal (37.6 vs 36.3). It is a more heavily specialized implementation than
  mine. The assignment's targets were `snprintf` and `std::to_string`, which FINAL beats at every
  magnitude on both machines. I keep `to_chars` in the table as an honest reference point.
- **8-digit chunks.** They help on the Mac for 12+ digits and on mixed. On the x86 VM they are a few
  percent slower than v3. I kept them because the Mac is my primary machine; v3 is the simpler
  alternative.
- **Small numbers and branches.** For 1–2 digits every version is under ~2 ns, so fixed overheads
  (counting digits, loop setup) dominate. On mixed input, the loop exits are unpredictable, so branch
  mispredictions dominate for every method, which is why the mixed column is the slowest.
- **Where `snprintf` and `to_string` lose their time.** `snprintf` parses the format string, goes
  through varargs and checks the locale. `std::to_string` builds a `std::string`. With libstdc++ that
  also means a heap allocation for 16+ digits, which is visible as the jump in its Linux row.
