# Dependency Inventory

## Overview

Agim has minimal external dependencies, relying primarily on the C standard library and POSIX extensions. This document inventories all dependencies for security review and compliance purposes.

## Build Dependencies

| Dependency | Version | Purpose | License |
|------------|---------|---------|---------|
| CMake | >= 3.10 | Build system | BSD-3-Clause |
| GCC or Clang | C11 compatible | Compiler | GPL/Apache-2.0 |

## Runtime Dependencies

### System Libraries (Linked)

| Library | Purpose | Notes |
|---------|---------|-------|
| `pthread` | POSIX threads | Multi-threaded scheduler, workers |
| `m` | Math library | Floating-point operations |
| `libc` | Standard C library | Core functionality |

### System Headers Used

#### Standard C (C11)

| Header | Purpose |
|--------|---------|
| `<stddef.h>` | Standard type definitions (size_t, NULL) |
| `<stdint.h>` | Fixed-width integer types |
| `<stdbool.h>` | Boolean type |
| `<stdatomic.h>` | Atomic operations (C11) |
| `<stdio.h>` | Standard I/O |
| `<stdlib.h>` | Memory allocation, conversions |
| `<string.h>` | String operations |
| `<limits.h>` | Implementation limits |
| `<errno.h>` | Error codes |
| `<inttypes.h>` | Integer format macros |
| `<time.h>` | Time functions |
| `<math.h>` | Math functions (via -lm) |

#### POSIX

| Header | Purpose |
|--------|---------|
| `<pthread.h>` | Threading primitives |
| `<unistd.h>` | POSIX API (fork, exec, file ops) |
| `<fcntl.h>` | File control |
| `<sys/time.h>` | Time structures |
| `<sys/select.h>` | I/O multiplexing |

#### Networking (Distribution module)

| Header | Purpose |
|--------|---------|
| `<sys/socket.h>` | Socket API |
| `<netinet/in.h>` | Internet address structures |
| `<netinet/tcp.h>` | TCP options |
| `<arpa/inet.h>` | Address conversion |
| `<netdb.h>` | Network database operations |

## Third-Party Code

### Embedded/Vendored

None. Agim contains no vendored third-party code.

### Algorithms Attribution

| Algorithm | Source | Location |
|-----------|--------|----------|
| SipHash | Reference implementation | `src/util/hash.c` |
| MPSC Queue | Dmitry Vyukov's design | `src/runtime/mailbox.c` |
| Chase-Lev Deque | Chase-Lev algorithm | `src/runtime/worker.c` |
| NaN-boxing | LuaJIT/SpiderMonkey inspired | `src/vm/nanbox.h` |

## Development Dependencies

These are only used during development/testing:

| Tool | Purpose | Required |
|------|---------|----------|
| Valgrind | Memory debugging | Optional |
| gcov/lcov | Code coverage | Optional |
| AddressSanitizer | Memory safety | Optional |
| ThreadSanitizer | Data race detection | Optional |
| MemorySanitizer | Uninitialized memory | Optional (Clang) |
| UBSanitizer | Undefined behavior | Optional |

## Platform Requirements

### Minimum Requirements

- **OS**: Linux (primary), macOS (secondary)
- **Architecture**: x86-64 (48-bit pointers for NaN-boxing)
- **C Standard**: C11
- **POSIX**: POSIX.1-2008

### Tested Platforms

| Platform | Status |
|----------|--------|
| Ubuntu 22.04 (x86-64) | Primary |
| Ubuntu 24.04 (x86-64) | Tested |
| Debian 12 (x86-64) | Tested |
| macOS 14+ (ARM64) | Experimental |

## Dependency Verification

### Build Verification

```bash
# Check CMake version
cmake --version

# Check compiler
gcc --version  # or clang --version

# Check pthread
ldconfig -p | grep pthread

# Check math library
ldconfig -p | grep libm
```

### Runtime Verification

```bash
# Check linked libraries
ldd build/agim

# Expected output:
#   linux-vdso.so.1
#   libpthread.so.0
#   libm.so.6
#   libc.so.6
```

## Security Considerations

1. **No network dependencies at build time**: All code is local
2. **Minimal runtime dependencies**: Only libc, pthread, math
3. **No dynamic loading**: No dlopen/dlsym except for potential future plugins
4. **No embedded crypto**: Uses OS random (/dev/urandom) for RNG needs
5. **POSIX-only networking**: No external HTTP/TLS libraries in core

## Update Policy

Since Agim has minimal dependencies:
- System libraries are managed by the OS package manager
- Build tools (CMake, GCC/Clang) should be kept current
- No third-party library updates needed

## License Compliance

All dependencies use permissive or copyleft licenses compatible with MIT:

| Component | License | Compatibility |
|-----------|---------|---------------|
| glibc | LGPL-2.1 | Yes (dynamic linking) |
| pthread | LGPL-2.1 | Yes (dynamic linking) |
| libm | LGPL-2.1 | Yes (dynamic linking) |
| GCC runtime | GPL + exception | Yes |
| Clang runtime | Apache-2.0 | Yes |
