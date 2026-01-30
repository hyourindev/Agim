# Contributing to Tofu

Thank you for your interest in contributing to Tofu! This document provides guidelines and instructions for contributing.

## Code of Conduct

Please be respectful and constructive in all interactions. We're building something together.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/tofu.git`
3. Create a branch: `git checkout -b feature/your-feature`
4. Make your changes
5. Run tests: `cd build && ctest`
6. Commit with a clear message
7. Push and open a Pull Request

## Development Setup

```bash
# Build in debug mode with sanitizers
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug -DTOFU_ENABLE_ASAN=ON ..
make

# Run tests
ctest --output-on-failure

# Format code
clang-format -i src/**/*.c src/**/*.h
```

## Code Style

- Follow the `.clang-format` configuration
- Use 4 spaces for indentation (no tabs)
- Keep lines under 100 characters
- Use `snake_case` for functions and variables
- Use `SCREAMING_SNAKE_CASE` for macros and constants
- Prefix public API functions with `tofu_`
- Prefix internal functions with module name (e.g., `vm_`, `gc_`)

### Naming Conventions

```c
// Public API
TofuValue *tofu_value_new_string(const char *str);

// Internal function
static Value *vm_stack_pop(VM *vm);

// Constants
#define TOFU_VERSION_MAJOR 0
#define TOFU_MAX_STACK_SIZE 1024

// Types
typedef struct TofuVM TofuVM;
typedef enum TofuOpcode TofuOpcode;
```

## Project Structure

```
src/
├── vm/           # Core bytecode VM
├── runtime/      # Block scheduler, messaging
├── soft/         # Soft Tofu compiler
├── firm/         # Firm Tofu compiler
└── builtin/      # Built-in tools
```

## Testing

- All new features must include tests
- All bug fixes must include regression tests
- Tests should be in the corresponding `tests/` subdirectory
- Use the simple test macros in `tests/test_common.h`

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

Use clear, descriptive commit messages:

```
component: short description

Longer explanation if needed. Explain the why, not just the what.

Fixes #123
```

Examples:
- `vm: add stack overflow detection`
- `soft/parser: fix indentation handling in nested blocks`
- `docs: update installation instructions`

## Pull Request Process

1. Ensure all tests pass
2. Update documentation if needed
3. Add yourself to CONTRIBUTORS.md (optional)
4. Request review from maintainers
5. Address feedback
6. Squash commits if requested

## Reporting Issues

When reporting bugs, please include:

- Tofu version (`tofu --version`)
- Operating system
- Minimal reproduction case
- Expected vs actual behavior
- Any error messages

## Feature Requests

For feature requests, please:

- Check existing issues first
- Describe the use case
- Explain why existing features don't suffice
- Consider implementation complexity

## Questions?

Open a Discussion on GitHub or reach out to the maintainers.

Thank you for contributing!
