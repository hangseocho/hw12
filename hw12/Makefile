# Week 12 — u64toa. Run inside hw12/; compiles against ../include/u64toa.hpp (the file CI grades).
#   make test    correctness tests (-O2)
#   make asan    correctness tests under AddressSanitizer + UBSan
#   make bench   cycles/op and ns/op vs snprintf / std::to_string / std::to_chars
CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -I../include
SANFLAGS  = -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -I../include
HDRS = ../include/u64toa.hpp u64toa_variants.hpp

all: test

test_u64toa: test_u64toa.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $< -o $@
test_u64toa_asan: test_u64toa.cpp $(HDRS)
	$(CXX) $(SANFLAGS) $< -o $@
bench_u64toa: bench_u64toa.cpp $(HDRS)
	$(CXX) $(CXXFLAGS) $< -o $@

test: test_u64toa
	./test_u64toa
asan: test_u64toa_asan
	./test_u64toa_asan
bench: bench_u64toa
	./bench_u64toa

clean:
	rm -f test_u64toa test_u64toa_asan bench_u64toa

.PHONY: all test asan bench clean
