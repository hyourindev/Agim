# Agim

> **Work in Progress** - This project is under active development. The `.im` implementation language works, but the `.ag` declarative agent DSL is not yet implemented. APIs may change.

**The Erlang for AI Agents** - Build fault-tolerant, distributed AI agent systems.

Agim is an Erlang-inspired language and runtime designed specifically for AI agents:

- **Lightweight processes** - Millions of concurrent agent processes (like Erlang)
- **Message passing** - Isolated agents communicate via async mailboxes
- **Fault tolerance** - "Let it crash" philosophy with supervisor trees
- **Capability-based security** - Fine-grained permission control for untrusted code
- **Built-in networking** - HTTP, WebSocket, TLS, TCP, SSE out of the box
- **LLM integration** - First-class tool calling and inference callbacks

## Quick Start

```bash
# Build
cmake -B build && cmake --build build

# Run a program
./build/agim examples/01_hello.im

# Run tests
cd build && ctest --output-on-failure

# Disassemble bytecode (see what the compiler generates)
./build/agim -d examples/01_hello.im
```

## File Extensions

| Extension | Description |
|-----------|-------------|
| `.im` | **Implementations** - Imperative tool/agent code (working) |
| `.ag` | **Agent workflows** - Declarative DSL (planned) |

The name **Agim** splits into its extensions: **AG** + **IM**

---

## The Language

Agim's `.im` language is a statically-aware, dynamically-typed language with:
- Erlang-style concurrency (processes, message passing)
- Rust-style error handling (Result/Option types)
- ML-style pattern matching
- First-class functions and closures
- Structs and algebraic data types (enums with payloads)

### Primitives

```
nil                     // Null value
true, false             // Booleans
42                      // Integers (64-bit signed)
3.14159                 // Floats (64-bit double)
"hello, world"          // Strings (UTF-8, immutable)
```

### Collections

```
// Arrays - dynamic, heterogeneous
let nums = [1, 2, 3, 4, 5]
let mixed = [1, "two", true, [3, 4]]

// Maps - string keys, any values
let person = {
    "name": "Alice",
    "age": 30,
    "tags": ["developer", "rust"]
}

// Access
nums[0]              // 1
person["name"]       // "Alice"
person.name          // "Alice" (dot syntax for string keys)
```

### Variables

```
let x = 42              // Immutable binding
let mut counter = 0     // Mutable binding
counter = counter + 1   // Mutation allowed
```

### Functions

```
// Basic function
fn add(a, b) {
    return a + b
}

// With type annotations (checked at compile time)
fn greet(name: string) -> string {
    return "Hello, " + name + "!"
}

// Default parameters
fn connect(host: string, port: int = 8080) {
    // ...
}

// First-class functions
let double = fn(x) { return x * 2 }
let nums = [1, 2, 3]
// map(nums, double)  // [2, 4, 6]
```

### Closures

```
fn make_counter() {
    let mut count = 0
    return fn() {
        count = count + 1
        return count
    }
}

let counter = make_counter()
counter()  // 1
counter()  // 2
counter()  // 3
```

### Control Flow

```
// If-else (expression, returns value)
let status = if x > 0 { "positive" } else { "non-positive" }

// While loops
while condition {
    do_something()
}

// For-in loops (arrays)
for item in items {
    print(item)
}

// Range loops
for i in 0..10 {       // 0 to 9 (exclusive end)
    print(i)
}

for i in 0..=10 {      // 0 to 10 (inclusive end)
    print(i)
}

// Break and continue
for i in 0..100 {
    if i == 50 { break }
    if i % 2 == 0 { continue }
    print(i)
}
```

### Structs

```
struct Point {
    x: int,
    y: int
}

struct Person {
    name: string,
    age: int,
    address: Address    // Nested structs
}

struct Address {
    city: string,
    zip: string
}

// Construction
let p = Point { x: 10, y: 20 }
let alice = Person {
    name: "Alice",
    age: 30,
    address: Address { city: "NYC", zip: "10001" }
}

// Access
print(p.x)                    // 10
print(alice.address.city)     // "NYC"
```

### Enums (Algebraic Data Types)

```
// Simple enum
enum Color {
    Red,
    Green,
    Blue
}

// Enum with payloads (tagged unions)
enum Shape {
    Circle(float),                    // radius
    Rectangle(float, float),          // width, height
    Point                             // no payload
}

enum Option<T> {
    Some(T),
    None
}

enum Result<T, E> {
    Ok(T),
    Err(E)
}

// Construction
let color = Color.Red
let shape = Shape.Circle(5.0)
let maybe = Option.Some(42)
```

### Pattern Matching

```
// Match on enums
match shape {
    Shape.Circle(r) => print("Circle with radius " + str(r)),
    Shape.Rectangle(w, h) => print("Rectangle " + str(w) + "x" + str(h)),
    Shape.Point => print("Just a point")
}

// Match on Result types
match fetch_data(url) {
    Ok(data) => process(data),
    Err(e) => print("Error: " + e)
}

// Match is an expression
let area = match shape {
    Shape.Circle(r) => 3.14159 * r * r,
    Shape.Rectangle(w, h) => w * h,
    Shape.Point => 0.0
}

// Guards (when clauses)
match value {
    n when n > 0 => "positive",
    n when n < 0 => "negative",
    _ => "zero"
}
```

### Result Types (Error Handling)

Agim uses Result types instead of exceptions:

```
fn divide(a: float, b: float) -> Result<float, string> {
    if b == 0.0 {
        return err("division by zero")
    }
    return ok(a / b)
}

// Check and unwrap
let result = divide(10.0, 2.0)
if is_ok(result) {
    print("Result: " + str(unwrap(result)))
}

// With default
let value = unwrap_or(divide(10.0, 0.0), 0.0)  // Returns 0.0 on error

// Pattern matching (preferred)
match divide(10.0, 2.0) {
    Ok(v) => print("Got: " + str(v)),
    Err(e) => print("Error: " + e)
}
```

### Modules

```
// math.im
export fn double(x) { return x * 2 }
export fn square(x) { return x * x }
export let PI = 3.14159

// Private (not exported)
fn helper() { ... }

// main.im
import "math.im"

print(double(5))    // 10
print(PI)           // 3.14159
```

### Tools (Agent-Callable Functions)

Tools are functions exposed to AI agents for tool calling:

```
// Using decorator syntax
@tool(description: "Search the web for information")
fn search(query: string) -> Result<[string], string> {
    return http_get("https://api.search.com?q=" + query)
}

// Using tool keyword
tool calculate(
    expression: string,
    description: "Evaluate a math expression"
) -> Result<float, string> {
    // Implementation
}

// List all registered tools (for LLM function calling)
let tools = list_tools()  // Returns tool schemas
```

### Concurrency (Actor Model)

Agim uses Erlang-style processes and message passing:

```
// Spawn a new process
let pid = spawn(fn() {
    // This runs in a separate process
    let msg = receive()  // Block until message arrives
    print("Got: " + str(msg))
})

// Send message to process
send(pid, "Hello from main!")

// Get own process ID
let me = self()

// Yield execution to other processes
yield()

// Example: ping-pong
let pong = spawn(fn() {
    while true {
        let msg = receive()
        print("Pong got: " + str(msg))
        send(msg.from, "pong")
    }
})

send(pong, { "type": "ping", "from": self() })
let response = receive()  // "pong"
```

---

## The Virtual Machine

Agim compiles to bytecode and runs on a custom VM with two execution modes:

### Stack-Based VM (`vm.c`)

The primary interpreter uses a stack-based architecture:

```
                    ┌─────────────────┐
                    │   Call Stack    │
                    ├─────────────────┤
                    │ Frame 2 (current)│ ← fp (frame pointer)
                    │   - locals      │
                    │   - operand stack│ ← sp (stack pointer)
                    ├─────────────────┤
                    │ Frame 1         │
                    ├─────────────────┤
                    │ Frame 0 (main)  │
                    └─────────────────┘
```

**Execution model:**
- **Bytecode format**: Variable-length instructions (1-3 bytes typically)
- **Operand stack**: All operations push/pop from the stack
- **Call frames**: Each function call creates a new frame with its own locals
- **Constants pool**: Literals stored separately, referenced by index

**Example compilation:**
```
Source:   let x = 1 + 2

Bytecode: OP_CONST 0      ; Push constant[0] (1)
          OP_CONST 1      ; Push constant[1] (2)
          OP_ADD          ; Pop two, push sum
          OP_SET_LOCAL 0  ; Store in local slot 0
```

### Register-Based VM (`regvm.c`)

An optimized interpreter using registers instead of a stack:

```
                    ┌─────────────────┐
                    │   Registers     │
                    │ r0  r1  r2  ... │  (256 registers per frame)
                    ├─────────────────┤
                    │   Instruction   │
                    │   [op|rd|rs1|rs2]│  (32-bit packed)
                    └─────────────────┘
```

**Benefits:**
- Fewer memory accesses (no stack push/pop overhead)
- Better instruction-level parallelism
- Computed goto dispatch (faster than switch)

**Instruction format:**
```c
// 32-bit instruction: [op:8][rd:8][rs1:8][rs2:8]
typedef struct {
    uint8_t op;    // Opcode
    uint8_t rd;    // Destination register
    uint8_t rs1;   // Source register 1
    uint8_t rs2;   // Source register 2 / immediate
} RegInstr;

// Example: ADD r0, r1, r2  (r0 = r1 + r2)
```

### Value Representation (NaN-Boxing)

Values are stored as 64-bit NaN-boxed doubles for efficiency:

```c
// NaN-boxing layout (64 bits):
//
// Doubles:  Normal IEEE 754 double values
// Tagged:   Use NaN space to encode other types
//
//   [1][11111111111][quiet][tag:3][payload:48]
//        exponent    NaN bit  type   value
//
// Tags:
//   000 = pointer (to heap object)
//   001 = nil
//   010 = false
//   011 = true
//   100 = integer (48-bit signed)
```

**Benefits:**
- Doubles stored directly (no boxing overhead for math)
- Small integers stored inline (no allocation)
- Pointers fit in 48 bits (x86-64 canonical addresses)
- Single 64-bit comparison for type checking

### Garbage Collection

Agim uses a **mark-sweep garbage collector** with generational hints:

```
                    ┌─────────────┐
      Roots ────────►│  Mark Phase │
   (stack, globals)  │  (traverse) │
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │ Sweep Phase │
                    │ (free dead) │
                    └─────────────┘
```

**Features:**
- **Write barriers**: Track cross-generation references
- **Incremental marking**: Reduce pause times
- **Reference counting**: For cycle-free structures (strings)
- **Copy-on-Write**: Shared structures until mutation

### Process Scheduler

The scheduler implements preemptive multitasking:

```
                    ┌─────────────────────────────────────┐
                    │            Scheduler                │
                    │  ┌─────────────────────────────┐   │
                    │  │      Run Queue (global)      │   │
                    │  └─────────────────────────────┘   │
                    │         │           │              │
                    │         ▼           ▼              │
                    │   ┌─────────┐ ┌─────────┐         │
                    │   │ Worker 0│ │ Worker 1│  ...    │
                    │   │ (thread)│ │ (thread)│         │
                    │   └────┬────┘ └────┬────┘         │
                    │        │           │              │
                    │   ┌────▼────┐ ┌────▼────┐         │
                    │   │ Deque 0 │ │ Deque 1 │         │
                    │   │(local q)│ │(local q)│         │
                    │   └─────────┘ └─────────┘         │
                    └─────────────────────────────────────┘
```

**Features:**
- **Reduction-based preemption**: Each process gets N reductions before yielding
- **Work-stealing**: Idle workers steal from busy workers' deques
- **Lock-free mailboxes**: MPSC queues for message passing
- **Timer wheel**: Efficient timeout handling

### Memory Allocators

Multiple allocation strategies for different use cases:

| Allocator | Use Case | Thread Safety |
|-----------|----------|---------------|
| `agim_alloc` | General allocation | Yes (malloc) |
| `worker_alloc` | Hot-path VM allocations | Thread-local (no locks) |
| `pool_alloc` | Fixed-size blocks | Mutex + free list |

---

## Architecture

```
agim/
├── src/
│   ├── vm/              # Virtual Machine
│   │   ├── vm.c         # Stack-based bytecode interpreter
│   │   ├── regvm.c      # Register-based VM (optimized)
│   │   ├── bytecode.c   # Opcode definitions
│   │   ├── value.c      # Value representation
│   │   ├── nanbox.h     # NaN-boxing implementation
│   │   ├── gc.c         # Garbage collector
│   │   └── ic.c         # Inline caching for fast property access
│   │
│   ├── lang/            # Language Frontend
│   │   ├── lexer.c      # Tokenizer (hand-written)
│   │   ├── parser.c     # Recursive descent parser
│   │   ├── compiler.c   # Bytecode code generator
│   │   ├── typechecker.c# Static type checking (optional annotations)
│   │   └── module.c     # Module system and imports
│   │
│   ├── runtime/         # Process Runtime (Erlang-style)
│   │   ├── scheduler.c  # Preemptive process scheduler
│   │   ├── worker.c     # Worker threads with work-stealing
│   │   ├── block.c      # Process ("block") management
│   │   ├── mailbox.c    # Lock-free MPSC message queues
│   │   ├── supervisor.c # Supervisor trees for fault tolerance
│   │   ├── capability.c # Capability-based security
│   │   ├── timer.c      # Hierarchical timer wheel
│   │   ├── hotreload.c  # Hot code reloading
│   │   ├── checkpoint.c # Process state checkpointing
│   │   └── serialize.c  # Value serialization
│   │
│   ├── net/             # Networking Stack
│   │   ├── tcp.c        # TCP client/server
│   │   ├── tls.c        # TLS/SSL via BearSSL
│   │   ├── http.c       # HTTP client
│   │   ├── http_parser.c# HTTP protocol parser
│   │   ├── websocket.c  # WebSocket client (RFC 6455)
│   │   ├── sse.c        # Server-Sent Events
│   │   └── url.c        # URL parsing
│   │
│   ├── dist/            # Distribution
│   │   └── node.c       # Distributed node communication
│   │
│   ├── types/           # Data Types
│   │   ├── array.c      # Dynamic arrays (COW)
│   │   ├── map.c        # Hash maps (COW)
│   │   ├── string.c     # Immutable strings
│   │   ├── vector.c     # Numeric vectors
│   │   └── closure.c    # Closures and upvalues
│   │
│   ├── builtin/         # Built-in Functions
│   │   ├── tools.c      # Tool registry for agents
│   │   ├── memory.c     # Persistent memory store
│   │   └── inference.c  # LLM inference callbacks
│   │
│   └── util/            # Utilities
│       ├── alloc.c      # Memory allocation wrappers
│       ├── pool.c       # Pool allocators
│       └── worker_alloc.c # Thread-local allocation
│
├── tests/               # Test suite
├── bench/               # Benchmarks
├── examples/            # Example programs (55+)
└── vendor/              # Dependencies (BearSSL)
```

---

## Security Model

Agim is designed to run **untrusted agent code** safely. Every process runs in a sandbox with explicit capabilities:

| Capability | Description | Default |
|------------|-------------|---------|
| `CAP_SPAWN` | Create new processes | Denied |
| `CAP_SEND` | Send messages to other processes | Denied |
| `CAP_RECEIVE` | Receive messages | Denied |
| `CAP_HTTP` | Make HTTP/HTTPS requests | Denied |
| `CAP_FILE_READ` | Read files from disk | Denied |
| `CAP_FILE_WRITE` | Write files to disk | Denied |
| `CAP_SHELL` | Execute shell commands | Denied |
| `CAP_EXEC` | Execute external processes | Denied |
| `CAP_ENV` | Access environment variables | Denied |
| `CAP_MEMORY` | Use persistent memory store | Denied |
| `CAP_INFER` | Call LLM inference | Denied |
| `CAP_WEBSOCKET` | WebSocket connections | Denied |

```c
// Capability enforcement in VM
if (!block_has_cap(vm->block, CAP_SHELL)) {
    vm_set_error(vm, "shell() requires CAP_SHELL capability");
    return VM_ERROR_PERMISSION;
}
```

---

## Built-in Functions

### Core
| Function | Description |
|----------|-------------|
| `print(x)` | Print to stdout |
| `print_err(x)` | Print to stderr |
| `len(x)` | Length of string/array/map |
| `type(x)` | Type name as string |
| `str(x)`, `int(x)`, `float(x)` | Type conversions |

### Collections
| Function | Description |
|----------|-------------|
| `push(arr, val)` | Append to array |
| `pop(arr)` | Remove last element |
| `keys(map)` | Get map keys as array |
| `slice(x, start, end)` | Slice string or array |

### Strings
| Function | Description |
|----------|-------------|
| `split(str, delim)` | Split into array |
| `join(arr, delim)` | Join array into string |
| `trim(str)` | Remove whitespace |
| `upper(str)`, `lower(str)` | Case conversion |
| `replace(str, old, new)` | Replace all occurrences |
| `contains(str, sub)` | Check substring |
| `starts_with`, `ends_with` | Check prefix/suffix |

### Results
| Function | Description |
|----------|-------------|
| `ok(value)` | Create Ok result |
| `err(message)` | Create Err result |
| `is_ok(r)`, `is_err(r)` | Check result type |
| `unwrap(r)` | Get value (panics on Err) |
| `unwrap_or(r, default)` | Get value or default |

### I/O & Network
| Function | Description |
|----------|-------------|
| `read_file(path)` | Read file contents |
| `write_file(path, data)` | Write to file |
| `http_get(url)` | HTTP GET request |
| `http_post(url, body)` | HTTP POST request |
| `ws_connect(url)` | WebSocket connection |
| `shell(cmd)` | Execute shell command |

### Concurrency
| Function | Description |
|----------|-------------|
| `spawn(fn)` | Create new process |
| `send(pid, msg)` | Send message |
| `receive()` | Wait for message |
| `self()` | Get own PID |
| `yield()` | Yield execution |

### Utilities
| Function | Description |
|----------|-------------|
| `time()` | Current timestamp (ms) |
| `sleep(ms)` | Sleep |
| `uuid()` | Generate UUID v4 |
| `json_parse(str)` | Parse JSON |
| `json_encode(val)` | Encode to JSON |
| `random()` | Random float 0-1 |

---

## Example Programs

The `examples/` directory contains 55+ programs:

| Range | Category |
|-------|----------|
| 01-10 | Basics: Hello World, Fibonacci, FizzBuzz, Primes, Arrays |
| 11-20 | Data Structures: Stack, Queue, File I/O, JSON |
| 21-25 | Networking: HTTP APIs, Shell Commands |
| 26-35 | Agent Utilities: Parsing, Time, Base64, Validation |
| 36-45 | Patterns: State Machine, Cache, Pipeline, Rate Limiter |
| 46-55 | Advanced: WebSocket, SSE, Process Exec, LLM Client |

---

## Building

### Requirements
- C11 compiler (GCC 7+ or Clang 5+)
- CMake 3.16+
- POSIX environment (Linux, macOS, WSL)

### Commands
```bash
# Release build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Debug with sanitizers
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DAGIM_ENABLE_ASAN=ON
cmake --build build

# Run tests
cd build && ctest --output-on-failure
```

---

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the full plan:
- Self-hosting compiler (bootstrap in Agim)
- Supervisor trees and process linking
- AI agent framework (behaviors, memory, tool dispatch)
- Distribution (multi-node clusters)
- REPL, debugger, package manager

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT License - see [LICENSE](LICENSE) for details.
