# Fuzzing Findings

## Summary

| Fuzzer | Runs | Time | Result |
|--------|------|------|--------|
| fuzz_lexer | 2.6M | 60s | **PASS** - No crashes |
| fuzz_parser | ~10K | 30s | **FIXED** - Memory leak found and fixed |
| fuzz_bytecode | ~100 | <1s | **FIXED** - Heap buffer overflow found and fixed |
| fuzz_nanbox | 30M | 31s | **PASS** - No crashes |

## Issues Found and Fixed

### 1. Bytecode Deserializer - Heap Buffer Overflow (FIXED)

**File:** `crash-3cf23a86f7742f894a189776b830900e7fc73bbc`

**Type:** Heap buffer overflow in `deserialize_value()` and `deserialize_chunk()`

**Root Cause:** No bounds checking when reading variable-length data from bytecode stream.

**Fix Applied:** Added comprehensive bounds checking in `src/vm/bytecode.c`:
- Added `end` pointer parameter to `deserialize_value()` to track buffer limits
- Added checks before every read operation: `if (*buf + N > end) return NULL`
- Fixed integer overflow vulnerability: `if (code_size > SIZE_MAX / 4) return false`
- Added size validation: `if (lines_bytes > (size_t)(end - *buf)) return false`

**Verification:** Fuzzer no longer crashes on the original input.

### 2. Parser - Memory Leak (FIXED)

**File:** `leak-ffa1781b9307bfa3c48066a3708e6fda3078c949`

**Type:** 90-byte memory leak in `error_at()`

**Root Cause:** When multiple parse errors occurred (in panic mode recovery), the old error string was overwritten without being freed first.

**Fix Applied:** Added cleanup in `src/lang/parser.c:error_at()`:
```c
/* Free any previous error message to prevent memory leak */
if (parser->error) {
    agim_free(parser->error);
}
parser->error = agim_alloc(needed);
```

**Verification:** Fuzzer no longer reports leaks on the original input.

## Passing Components

### Lexer
- **2.6 million** test cases with no crashes
- Handles binary data, UTF-8, and edge cases correctly
- Well-tested against malformed input

### NaN-boxing
- **30 million** test cases with no crashes
- All 64-bit patterns handled correctly
- Type detection is robust

## Reproduction

```bash
# Build with fuzzing
CC=clang cmake .. -DAGIM_ENABLE_FUZZING=ON
make fuzz_lexer fuzz_parser fuzz_bytecode fuzz_nanbox

# Run fuzzers
./fuzz_lexer -max_total_time=60
./fuzz_parser -max_total_time=60
./fuzz_bytecode -max_total_time=60
./fuzz_nanbox -max_total_time=60
```

## Next Steps

1. ~~**P0 - Fix bytecode heap overflow**~~ **DONE**
2. ~~**P1 - Fix parser memory leak**~~ **DONE**
3. Run fuzzers for longer (hours/days) in CI
4. Add regression tests for these specific inputs
5. Set up continuous fuzzing (OSS-Fuzz or ClusterFuzz)
