# Agim

> **WIP** - This project is under active development. The `.im` implementation language works, but the `.ag` declarative agent DSL is not yet implemented.

**Build agents. Run anywhere.**

A language and runtime for building performant, isolated AI agents.

## File Extensions

| Extension | Description |
|-----------|-------------|
| `.ag` | Agent workflows - declarative DSL (planned) |
| `.im` | Implementations - imperative tool code (current) |

The name **Agim** splits into its extensions: **AG** + **IM**

## Quick Start

```bash
# Build
cmake -B build && cmake --build build

# Run a program
./build/agim examples/01_hello.im

# Run tests
cd build && ctest --output-on-failure
```

## Language Features

### Data Types
- `nil`, `true`, `false` - Primitives
- `42`, `3.14` - Numbers (int and float)
- `"hello"` - Strings
- `[1, 2, 3]` - Arrays
- `{"key": "value"}` - Maps

### Variables & Functions
```
let x = 42
let name = "Agim"

fn greet(who) {
    return "Hello, " + who + "!"
}
```

### Control Flow
```
if x > 0 {
    print("positive")
} else {
    print("non-positive")
}

while x > 0 {
    x = x - 1
}

for item in items {
    print(item)
}
```

### Tools (Agent-Exposed Functions)
```
@tool(description: "Add two numbers")
fn add(a, b) {
    return a + b
}

// Or using the tool keyword
tool add(a: number, b: number) -> number {
    return a + b
}
```

### Result Types (Error Handling)
```
fn safe_divide(a, b) {
    if b == 0 {
        return err("division by zero")
    }
    return ok(a / b)
}

let result = safe_divide(10, 2)
match result {
    ok(val) => print("Result: " + str(val))
    err(e) => print("Error: " + e)
}

// Or use is_ok/is_err/unwrap
if is_ok(result) {
    print(unwrap(result))
}
```

### Modules
```
// math.im
export fn double(x) { return x * 2 }
export let PI = 314

// main.im
import "math.im"
print(double(5))    // 10
print(PI)           // 314
```

## Built-in Functions

### Core
| Function | Description |
|----------|-------------|
| `print(x)` | Print a value |
| `print_err(x)` | Print to stderr |
| `len(x)` | Length of string/array/map |
| `type(x)` | Get type name as string |
| `keys(map)` | Get array of map keys |
| `push(arr, val)` | Push value to array |
| `pop(arr)` | Pop value from array |
| `slice(x, start, end)` | Slice string or array |
| `str(x)` | Convert to string |
| `int(x)` | Convert to integer |
| `float(x)` | Convert to float |

### Result Types
| Function | Description |
|----------|-------------|
| `ok(value)` | Create success Result |
| `err(message)` | Create error Result |
| `is_ok(result)` | Check if Result is ok |
| `is_err(result)` | Check if Result is err |
| `unwrap(result)` | Get value (panics on err) |
| `unwrap_or(result, default)` | Get value or default |

### String Operations
| Function | Description |
|----------|-------------|
| `split(str, delim)` | Split string into array |
| `join(arr, delim)` | Join array into string |
| `trim(str)` | Trim whitespace |
| `upper(str)` | Convert to uppercase |
| `lower(str)` | Convert to lowercase |
| `replace(str, old, new)` | Replace all occurrences |
| `contains(str, sub)` | Check if contains substring |
| `starts_with(str, prefix)` | Check prefix |
| `ends_with(str, suffix)` | Check suffix |
| `index_of(str, sub)` | Find substring index (-1 if not found) |
| `char_at(str, idx)` | Get character at index |

### Math
| Function | Description |
|----------|-------------|
| `abs(n)` | Absolute value |
| `floor(n)` | Floor (round down) |
| `ceil(n)` | Ceiling (round up) |
| `round(n)` | Round to nearest |
| `sqrt(n)` | Square root |
| `pow(base, exp)` | Power/exponent |
| `min(a, b)` | Minimum of two values |
| `max(a, b)` | Maximum of two values |
| `random()` | Random float 0-1 |
| `random_int(min, max)` | Random integer in range |

### Time
| Function | Description |
|----------|-------------|
| `time()` | Current timestamp in milliseconds |
| `time_format(ts, fmt)` | Format timestamp (strftime format) |
| `sleep(ms)` | Sleep for milliseconds |

### JSON
| Function | Description |
|----------|-------------|
| `json_parse(str)` | Parse JSON string to value |
| `json_encode(val)` | Encode value to JSON string |

### Base64
| Function | Description |
|----------|-------------|
| `base64_encode(str)` | Encode to base64 |
| `base64_decode(str)` | Decode from base64 |

### Environment
| Function | Description |
|----------|-------------|
| `env_get(name)` | Get environment variable |
| `env_set(name, val)` | Set environment variable |

### File I/O
| Function | Description |
|----------|-------------|
| `read_file(path)` | Read file contents (returns Result) |
| `write_file(path, content)` | Write to file (returns Result) |
| `file_exists(path)` | Check if file exists |
| `read_lines(path)` | Read file as array of lines (returns Result) |
| `read_stdin()` | Read from standard input |

### HTTP & Shell
| Function | Description |
|----------|-------------|
| `http_get(url)` | HTTP GET request (returns Result) |
| `http_post(url, body)` | HTTP POST request (returns Result) |
| `shell(command)` | Execute shell command (returns Result) |

### WebSocket
| Function | Description |
|----------|-------------|
| `ws_connect(url)` | Connect to WebSocket server |
| `ws_send(handle, msg)` | Send message to WebSocket |
| `ws_recv(handle)` | Receive message from WebSocket |
| `ws_close(handle)` | Close WebSocket connection |

### HTTP Streaming (SSE)
| Function | Description |
|----------|-------------|
| `http_stream(url)` | Open HTTP streaming connection |
| `stream_read(handle)` | Read next line from stream |
| `stream_close(handle)` | Close streaming connection |

### Process Execution
| Function | Description |
|----------|-------------|
| `exec(cmd, input)` | Execute command with stdin input, returns stdout |
| `exec_async(cmd)` | Start async process, returns handle |
| `proc_write(handle, data)` | Write to process stdin |
| `proc_read(handle)` | Read from process stdout |
| `proc_close(handle)` | Close process handle |

### UUID & Hashing
| Function | Description |
|----------|-------------|
| `uuid()` | Generate UUID v4 |
| `hash_md5(str)` | Compute MD5 hash |
| `hash_sha256(str)` | Compute SHA256 hash |

### Concurrency (Scheduler Mode)
These functions require running with the scheduler (multi-block mode):
| Function | Description |
|----------|-------------|
| `spawn(fn)` | Spawn a new block running `fn`, returns PID |
| `send(pid, value)` | Send a message to block with `pid` |
| `receive()` | Wait for and receive a message |
| `self()` | Get current block's PID |
| `yield()` | Yield execution to other blocks |

Note: The CLI currently runs in single-block mode. Concurrency primitives are available for embedding Agim in applications that use the scheduler API.

## Example Programs

### Basics (01-10)
| # | File | Description |
|---|------|-------------|
| 01 | `hello.im` | Hello World |
| 02 | `fibonacci.im` | Recursive Fibonacci |
| 03 | `fizzbuzz.im` | FizzBuzz 1-30 |
| 04 | `primes.im` | Prime numbers up to 50 |
| 05 | `factorial.im` | Factorial (recursive & iterative) |
| 06 | `arrays.im` | Array operations (sum, max, min, avg) |
| 07 | `maps.im` | Map operations and nested maps |
| 08 | `calculator.im` | Calculator tools |
| 09 | `strings.im` | String manipulation |
| 10 | `sorting.im` | Bubble sort & selection sort |

### Data Structures (11-20)
| # | File | Description |
|---|------|-------------|
| 11 | `file_io.im` | File read/write operations |
| 12 | `types.im` | Type checking and conversion |
| 13 | `stack.im` | Stack data structure |
| 14 | `json_like.im` | Nested JSON-like structures |
| 15 | `slicing.im` | String and array slicing |
| 16 | `queue.im` | Queue data structure |
| 17 | `word_tools.im` | Word processing utilities |
| 18 | `math_tools.im` | Math utilities (gcd, lcm, sqrt) |
| 19 | `config_parser.im` | Config file parser |
| 20 | `todo_app.im` | Todo app with file persistence |

### I/O & Network (21-25)
| # | File | Description |
|---|------|-------------|
| 21 | `http_api.im` | HTTP API requests |
| 22 | `shell_commands.im` | Shell command execution |
| 23 | `json_api.im` | JSON parsing from APIs |
| 24 | `web_scraper.im` | Simple web scraping |
| 25 | `system_monitor.im` | System monitoring tool |

### Agent Utilities (26-35)
| # | File | Description |
|---|------|-------------|
| 26 | `json_tools.im` | JSON parsing and encoding |
| 27 | `string_utils.im` | Advanced string operations |
| 28 | `time_date.im` | Timestamps and formatting |
| 29 | `random.im` | Random number generation |
| 30 | `math_advanced.im` | Advanced math functions |
| 31 | `base64.im` | Base64 encoding/decoding |
| 32 | `env_vars.im` | Environment variables |
| 33 | `text_parser.im` | Parse CSV, key=value, query strings |
| 34 | `data_transform.im` | Map, filter, reduce patterns |
| 35 | `api_client.im` | HTTP client with retry logic |

### Patterns (36-45)
| # | File | Description |
|---|------|-------------|
| 36 | `logging.im` | Structured logging system |
| 37 | `task_runner.im` | Sequential/parallel task execution |
| 38 | `state_machine.im` | Finite state machine pattern |
| 39 | `prompt_builder.im` | Build prompts for LLMs |
| 40 | `cache.im` | Caching patterns (TTL, LRU) |
| 41 | `validation.im` | Input validation utilities |
| 42 | `pipeline.im` | Data processing pipelines |
| 43 | `retry_backoff.im` | Retry with exponential backoff |
| 44 | `rate_limiter.im` | Token bucket rate limiting |
| 45 | `event_system.im` | Pub/sub event handling |

### Tools (46-50)
| # | File | Description |
|---|------|-------------|
| 46 | `markdown_gen.im` | Markdown document generator |
| 47 | `cli_args.im` | CLI argument parsing |
| 48 | `agent_tools.im` | Tool registry for AI agents |
| 49 | `streaming.im` | Stream processing patterns |
| 50 | `template_engine.im` | Template rendering engine |

### Networking & Advanced (51-55)
| # | File | Description |
|---|------|-------------|
| 51 | `websocket.im` | WebSocket communication patterns |
| 52 | `http_streaming.im` | HTTP streaming (SSE) patterns |
| 53 | `process_exec.im` | Process execution, UUID, hashing |
| 54 | `llm_client.im` | LLM API client patterns |
| 55 | `safe_math_tools.im` | Safe math with Result types |

## Project Structure

```
agim/
├── src/
│   ├── vm/           # Core bytecode VM
│   │   ├── value.c   # Value representation
│   │   ├── bytecode.c# Bytecode format
│   │   ├── vm.c      # Interpreter loop
│   │   └── gc.c      # Garbage collector
│   ├── lang/         # Agim compiler
│   │   ├── lexer.c   # Tokenizer
│   │   ├── parser.c  # Parser
│   │   └── compiler.c# Bytecode compiler
│   └── runtime/      # Process runtime
├── bench/            # Benchmarks
├── cmd/              # CLI entry point
├── editor/           # Editor support (tree-sitter, zed)
├── examples/         # Example programs (.im)
├── scripts/          # Shell scripts
└── tests/            # Test suite
```

## CLI Usage

```
$ agim --help
Agim - A language for building isolated AI agents

Usage: agim [options] <file.im>

File extensions:
  .ag            Agent workflow (declarative) [planned]
  .im            Implementation (imperative)

Options:
  -h, --help     Show this help message
  -v, --version  Show version information
  -d, --disasm   Disassemble bytecode instead of running
  -t, --tools    List registered tools
```

## License

MIT License - see [LICENSE](LICENSE) for details.
