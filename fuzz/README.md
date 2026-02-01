# Agim Fuzzing

This directory contains fuzz targets for testing Agim components with libFuzzer.

## Requirements

- Clang compiler (libFuzzer is built into Clang)
- CMake 3.10+

## Building

```bash
# Clean build directory
rm -rf build && mkdir build && cd build

# Configure with fuzzing enabled (requires Clang)
CC=clang cmake .. -DAGIM_ENABLE_FUZZING=ON

# Build all fuzzers
make fuzz_lexer fuzz_parser fuzz_bytecode fuzz_nanbox
```

## Running Fuzzers

### Lexer Fuzzer
```bash
./fuzz_lexer ../fuzz/corpus/lexer/ -dict=../fuzz/lexer.dict -max_len=4096
```

### Parser Fuzzer
```bash
./fuzz_parser ../fuzz/corpus/parser/ -dict=../fuzz/lexer.dict -max_len=4096
```

### Bytecode Fuzzer
```bash
./fuzz_bytecode ../fuzz/corpus/bytecode/ -max_len=4096
```

### NaN-box Fuzzer
```bash
./fuzz_nanbox ../fuzz/corpus/nanbox/ -max_len=64
```

## Common Options

- `-max_len=N`: Maximum input size in bytes
- `-max_total_time=N`: Run for N seconds
- `-jobs=N`: Run N parallel jobs
- `-workers=N`: Use N worker processes
- `-dict=FILE`: Use dictionary file for mutations
- `-only_ascii=1`: Only use ASCII characters
- `-help=1`: Show all options

## Crash Reproduction

When a crash is found, libFuzzer saves the crashing input to `crash-*` or `oom-*`.
To reproduce:

```bash
./fuzz_lexer crash-abc123
```

## Coverage-Guided Fuzzing

libFuzzer uses coverage feedback to guide mutations. To see coverage:

```bash
# Run with coverage profiling
./fuzz_lexer corpus/ -runs=10000
# Coverage data is tracked internally
```

## Corpus Management

- `corpus/lexer/`: Seed inputs for lexer fuzzer
- `corpus/parser/`: Seed inputs for parser fuzzer
- `corpus/bytecode/`: Seed inputs for bytecode fuzzer
- `corpus/nanbox/`: Seed inputs for NaN-box fuzzer

Add valid Agim source files to the corpus to improve fuzzing effectiveness.

## Continuous Fuzzing

For integration with OSS-Fuzz or ClusterFuzz, see the project-level documentation.
