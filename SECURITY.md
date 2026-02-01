# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 0.x.x   | :white_check_mark: |

## Security Model

Agim implements a **capability-based security model** inspired by Erlang/OTP but enhanced for AI agent workloads. Every block (lightweight process) has an explicit set of capabilities that control what operations it can perform.

### Capabilities

Blocks are created with `CAP_NONE` by default (deny-all). Capabilities must be explicitly granted:

| Capability | Description |
|------------|-------------|
| `CAP_SPAWN` | Create new blocks |
| `CAP_SEND` | Send messages to other blocks |
| `CAP_RECEIVE` | Receive messages |
| `CAP_INFER` | Call LLM inference APIs |
| `CAP_HTTP` | Make HTTP requests |
| `CAP_FILE_READ` | Read files (within sandbox) |
| `CAP_FILE_WRITE` | Write files (within sandbox) |
| `CAP_DB` | Access database connections |
| `CAP_MEMORY` | Direct memory operations |
| `CAP_LINK` | Link to other blocks |
| `CAP_SHELL` | Execute shell commands |
| `CAP_EXEC` | Execute external programs |
| `CAP_TRAP_EXIT` | Receive exit signals as messages |
| `CAP_MONITOR` | Monitor other blocks |
| `CAP_SUPERVISE` | Supervise child blocks |
| `CAP_ENV` | Access environment variables |
| `CAP_WEBSOCKET` | Use WebSocket connections |

### Sandbox

File operations are restricted by a configurable sandbox:
- Path whitelists for read/write directories
- Path canonicalization to prevent traversal attacks
- Symlinks are not followed outside the sandbox
- Default configuration is restrictive (no file access)

### Process Isolation

- Each block has its own heap (no shared memory)
- Blocks communicate only via message passing
- GC runs per-block (no global pauses)
- One block crashing cannot corrupt another's memory

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Instead, please report security vulnerabilities by emailing: security@example.com

You should receive a response within 48 hours. If the issue is confirmed, we will:
1. Work with you to understand and reproduce the issue
2. Develop a fix and prepare a security advisory
3. Credit you in the advisory (unless you prefer to remain anonymous)
4. Release a patched version

### What to Include

Please include:
- Type of issue (buffer overflow, injection, capability bypass, etc.)
- Source file(s) and line number(s) if known
- Step-by-step reproduction instructions
- Proof-of-concept code if available
- Impact assessment

## Security Best Practices

### For Users

1. **Use minimal capabilities**: Only grant capabilities that blocks actually need
2. **Configure the sandbox**: Explicitly whitelist directories for file access
3. **Validate inputs**: Use the type checker and validate untrusted data
4. **Monitor supervisors**: Set up proper supervision trees for fault tolerance
5. **Limit resources**: Use block limits to prevent resource exhaustion

### For Contributors

1. **No unsafe patterns**:
   - No `gets()`, use bounded input functions
   - No `sprintf()`, use `snprintf()`
   - No unchecked array indexing
   - No pointer arithmetic without bounds checks

2. **Memory safety**:
   - Initialize all variables
   - Check all allocations for NULL
   - Use RAII patterns where possible
   - Free memory exactly once

3. **Capability checks**:
   - Always check capabilities before privileged operations
   - Use `block_check_cap()` which crashes on failure
   - Never bypass capability checks

4. **Input validation**:
   - Validate all external inputs
   - Canonicalize paths before file operations
   - Check sizes before allocations

5. **Concurrency**:
   - Use per-block mutexes for link/monitor arrays
   - Use atomics for cross-thread counters
   - Never hold locks while calling external functions

## Build-Time Security

### Compiler Hardening

The build enables:
- `-Wall -Wextra` - Comprehensive warnings
- `-Wformat=2 -Wformat-security` - Format string checks
- `-fstack-protector-strong` - Stack canaries
- `-Wnull-dereference` - Null pointer detection
- `-Warray-bounds` - Array bounds checking

### Sanitizers

Available via CMake options:
- `AGIM_ENABLE_ASAN` - AddressSanitizer
- `AGIM_ENABLE_MSAN` - MemorySanitizer (Clang only)
- `AGIM_ENABLE_UBSAN` - UndefinedBehaviorSanitizer
- `AGIM_ENABLE_TSAN` - ThreadSanitizer

Run tests with sanitizers enabled:
```bash
cmake .. -DAGIM_ENABLE_ASAN=ON
make -j4
ctest
```

## Known Limitations

1. **No encryption at rest**: Block heaps are not encrypted
2. **No network isolation**: HTTP/WebSocket capabilities are all-or-nothing per block
3. **Supervisor trust**: Supervisors have broad access to child blocks
4. **Shell capability**: `CAP_SHELL` allows arbitrary shell commands within the host

## Security Changelog

### v0.1.0 (Current Development)
- Implemented capability-based security model
- Added file sandbox with path canonicalization
- Enabled comprehensive compiler warnings
- Added sanitizer support (ASan, MSan, UBSan, TSan)
