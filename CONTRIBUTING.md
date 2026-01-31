# Contributing to Agim

Thank you for your interest in contributing to Agim! This document provides guidelines and instructions for contributing.

## Code of Conduct

Please read and follow our [Code of Conduct](CODE_OF_CONDUCT.md). We're building something together and expect respectful, constructive interactions.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/agim.git`
3. Create a branch: `git checkout -b feature/your-feature`
4. Make your changes
5. Run tests: `cd build && ctest --output-on-failure`
6. Commit with a clear message
7. Push and open a Pull Request

## Development Setup

```bash
# Clone
git clone https://github.com/YOUR_USERNAME/agim.git
cd agim

# Debug build with sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DAGIM_ENABLE_ASAN=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure

# Run security tests
./tests/security/test_security

# Run benchmarks
./bench/bench_vm

# Format code
find src -name "*.c" -o -name "*.h" | xargs clang-format -i
```

## Project Structure

```
src/
├── vm/              # Virtual machine (stack + register based)
├── lang/            # Lexer, parser, compiler, typechecker
├── runtime/         # Scheduler, blocks, mailboxes, supervisors
├── net/             # TCP, TLS, HTTP, WebSocket, SSE
├── dist/            # Distributed node communication
├── types/           # Array, map, string, vector, closure
├── builtin/         # Tools, memory, inference
├── util/            # Allocation, pools
└── debug/           # Debugging utilities

tests/
├── vm/              # VM tests
├── lang/            # Parser/compiler tests
├── runtime/         # Scheduler/block tests
├── types/           # Data type tests
├── net/             # Network tests
└── security/        # Security tests (IMPORTANT!)
```

## Code Style

- Use 4 spaces (no tabs)
- Lines under 100 characters
- `snake_case` for functions/variables
- `SCREAMING_SNAKE_CASE` for macros/constants
- Prefix functions with module name (`vm_`, `gc_`, `scheduler_`)

### Example

```c
/* Internal functions - prefix with module */
static Value *vm_stack_pop(VM *vm);
static void scheduler_run_block(Scheduler *sched, Block *block);

/* Constants */
#define VM_MAX_STACK_SIZE 1024
#define CAP_HTTP (1 << 4)

/* Types */
typedef struct VM VM;
typedef enum Capability Capability;
```

### Security-Sensitive Code

```c
/* GOOD: Bounds check */
if (index >= array->length) {
    return NULL;
}

/* GOOD: Overflow protection */
if (capacity > SIZE_MAX / 2) {
    return false;
}
capacity *= 2;

/* GOOD: Capability check */
if (!block_has_cap(vm->block, CAP_SHELL)) {
    vm_set_error(vm, "requires CAP_SHELL");
    return VM_ERROR_PERMISSION;
}
```

## Testing

- **All new features need tests**
- **All bug fixes need regression tests**
- **Security changes need security tests**

```c
#include "test_common.h"

void test_something(void) {
    ASSERT(1 + 1 == 2);
    ASSERT_EQ(42, calculate());
    ASSERT_STR_EQ("hello", get_greeting());
}

int main(void) {
    RUN_TEST(test_something);
    return TEST_RESULT();
}
```

## Commit Messages

```
component: short description

Longer explanation if needed.

Fixes #123
```

Components: `vm`, `lang`, `runtime`, `net`, `dist`, `types`, `builtin`, `util`, `security`, `docs`, `tests`, `bench`

Examples:
- `vm: add stack overflow detection`
- `security: add CAP_ENV for environment access`
- `tls: enable hostname verification`

## Pull Requests

1. Ensure all tests pass
2. Run security tests for security changes
3. Update docs if needed
4. Write clear PR description
5. Address review feedback

### Checklist

- [ ] Tests pass locally
- [ ] Security tests pass (if applicable)
- [ ] Code follows style guidelines
- [ ] No new warnings
- [ ] Documentation updated

## Security Vulnerabilities

If you discover a security issue:

1. **DO NOT** open a public issue
2. Email maintainers directly
3. Allow time for a fix before disclosure

## Questions?

- Open a GitHub Discussion
- Check existing issues
- Read [ROADMAP.md](ROADMAP.md)

Thank you for contributing!
