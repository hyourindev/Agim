# Fuzzing Findings

## Summary

| Fuzzer | Runs | Time | Result |
|--------|------|------|--------|
| fuzz_lexer | 2.6M | 60s | **PASS** - No crashes |
| fuzz_parser | ~10K | 30s | **LEAK** - Memory leak found |
| fuzz_bytecode | ~100 | <1s | **CRASH** - Heap buffer overflow |
| fuzz_nanbox | 30M | 31s | **PASS** - No crashes |

## Issues Found

### 1. Bytecode Deserializer - Heap Buffer Overflow (CRITICAL)

**File:** `crash-3cf23a86f7742f894a189776b830900e7fc73bbc`

**Type:** Heap buffer overflow in `bytecode_deserialize()`

**Trigger:** Malformed bytecode with valid magic header but corrupted size fields

**Input (hex):**
```
4147 494d 0000 0000 0000 0000 0000 4000  AGIM..........@.
0000 0000 0032 0001 b7b7 b7b7 b7b7 b7b7  .....2..........
...
```

**Stack trace:**
- `bytecode_deserialize` at bytecode.c
- Uses size field (0x40 = 64 at offset 14) but data is malformed

**Recommendation:** Add bounds checking in bytecode_deserialize() for all size fields before allocation/copy.

### 2. Parser - Memory Leak (MEDIUM)

**File:** `leak-ffa1781b9307bfa3c48066a3708e6fda3078c949`

**Type:** 90-byte memory leak in parser allocation

**Trigger:** Malformed input with garbage bytes

**Input (hex):**
```
90df 9c6f 816d 656e 7490 0a  ...o.ment..
```

**Stack trace:**
- `parser_new()` at parser.c:1573
- Allocates memory that isn't freed on parse error

**Recommendation:** Ensure parser_free() is called even when parser_parse() fails, or fix the leak in error paths.

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

# Reproduce crashes
./fuzz_bytecode fuzz/crashes/crash-3cf23a86f7742f894a189776b830900e7fc73bbc
./fuzz_parser fuzz/crashes/leak-ffa1781b9307bfa3c48066a3708e6fda3078c949
```

## Next Steps

1. **P0 - Fix bytecode heap overflow** - Add size validation before memory operations
2. **P1 - Fix parser memory leak** - Ensure cleanup on all error paths
3. Run fuzzers for longer (hours/days) in CI
4. Add regression tests for these specific inputs
