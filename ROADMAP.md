# Tofulang/Agim Roadmap: The Erlang for AI Agents

## Vision

Make Tofulang the **go-to language for building AI agents**, just as Erlang is for telecom/distributed systems.

**Core Principles:**
- Lightweight agent processes (like Erlang processes)
- Message passing between agents
- "Let it crash" fault tolerance with supervisors
- Hot code reloading for live agent updates
- Built-in LLM integration
- Tool calling as a first-class concept

---

## Current State Analysis

### Already Implemented (C Level)

| Component | Status | Notes |
|-----------|--------|-------|
| **Scheduler** | ✅ Done | Reduction-based preemption, multi-threaded workers, work-stealing |
| **Blocks/Processes** | ✅ Done | PID-based, capabilities, limits |
| **Mailboxes** | ✅ Done | Lock-free MPSC queues |
| **Message Passing** | ✅ Done | `send()`, `receive()` in bytecode |
| **Spawn** | ✅ Done | `spawn()` creates new blocks |
| **Inference Callbacks** | ✅ Done | LLM inference hooks |
| **Tools Registry** | ✅ Done | @tool decorator, tool introspection |

### Already Implemented (Language Level)

| Feature | Status | Notes |
|---------|--------|-------|
| Structs | ✅ Done | With nested fields |
| Enums + Payloads | ✅ Done | Pattern matching |
| Result/Option | ✅ Done | Error handling |
| Match Expressions | ✅ Done | With blocks, returns |
| HTTP (all methods) | ✅ Done | GET/POST/PUT/PATCH/DELETE/custom |
| WebSocket | ✅ Done | connect/send/recv/close |
| File I/O | ✅ Done | read/write/exists/lines |
| JSON | ✅ Done | encode/parse |
| Imports | ✅ Done | Module system |
| Range Loops | ✅ Done | 0..n and 0..=n |

### Not Yet Exposed to Language

| Feature | C Status | Language Status |
|---------|----------|-----------------|
| spawn() | ✅ Bytecode exists | ⚠️ Needs runtime setup |
| send() | ✅ Bytecode exists | ⚠️ Needs runtime setup |
| receive() | ✅ Bytecode exists | ⚠️ Needs runtime setup |
| self() | ✅ Bytecode exists | ⚠️ Needs runtime setup |
| Supervisors | ❌ Not implemented | ❌ |
| Process linking | ❌ Not implemented | ❌ |
| LLM inference | ✅ Callback exists | ❌ Not exposed |

---

## Phase 1: Bootstrap Compiler (Self-Hosting)

**Goal:** Write the Tofulang compiler in Tofulang itself.

**Duration:** 1-2 weeks

### 1.1 Add Missing Primitives

```
fs.write_bytes(path, [int])  -> Write byte array as binary
```

**Files to modify:**
- `src/vm/bytecode.h` - Add `OP_FILE_WRITE_BYTES`
- `src/vm/vm.c` - Implement the opcode
- `src/lang/compiler.c` - Add `fs.write_bytes()` compilation

### 1.2 Write Lexer in Tofulang

```tofu
enum TokenType {
    EOF, Error(string),
    // Literals
    Int(int), Float(float), String(string), Ident(string),
    // Keywords
    Fn, Let, If, Else, For, While, Return, ...
    // Operators
    Plus, Minus, Star, Slash, ...
}

struct Token {
    type: TokenType,
    line: int,
    column: int
}

struct Lexer {
    source: string,
    pos: int,
    line: int,
    column: int
}

fn lex(source: string) -> Result<[Token], string>
```

### 1.3 Write Parser in Tofulang

```tofu
enum AstNode {
    Program([AstNode]),
    FnDecl { name: string, params: [Param], body: AstNode },
    Block([AstNode]),
    Let { name: string, value: AstNode },
    If { cond: AstNode, then: AstNode, else: Option<AstNode> },
    Binary { op: BinOp, left: AstNode, right: AstNode },
    Call { callee: AstNode, args: [AstNode] },
    ...
}

fn parse(tokens: [Token]) -> Result<AstNode, string>
```

### 1.4 Write Code Generator in Tofulang

```tofu
struct Compiler {
    bytecode: [int],
    constants: [any],
    locals: [string]
}

fn compile(ast: AstNode) -> Result<[int], string>
```

### 1.5 Bootstrap

1. Compile the Tofulang compiler using the C compiler
2. Use the resulting program to compile itself
3. Verify output matches

---

## Phase 2: Actor Model (Erlang-Style Concurrency)

**Goal:** Expose existing C infrastructure to Tofulang with ergonomic syntax.

**Duration:** 2-3 weeks

### 2.1 Process Primitives

```tofu
// Spawn a new process
let pid = spawn(fn() {
    let msg = receive()
    print("Got: " + str(msg))
})

// Send message
send(pid, "Hello!")

// Get own PID
let me = self()

// Receive with timeout
let msg = receive_timeout(5000)  // 5 second timeout
match msg {
    some(m) => handle(m)
    none => print("Timeout!")
}

// Selective receive (pattern matching on messages)
receive {
    {type: "request", data: d} => handle_request(d)
    {type: "quit"} => return
    _ => print("Unknown message")
}
```

### 2.2 Process Linking & Monitoring

```tofu
// Link processes (bidirectional crash propagation)
link(pid)

// Monitor process (receive notification on crash)
let ref = monitor(pid)

// Trap exits (become supervisor)
trap_exit(true)

receive {
    {exit: pid, reason: r} => handle_crash(pid, r)
    msg => handle(msg)
}
```

### 2.3 Supervisors

```tofu
enum RestartStrategy {
    OneForOne,      // Restart only crashed child
    OneForAll,      // Restart all children
    RestForOne      // Restart crashed + children after it
}

struct ChildSpec {
    id: string,
    start: fn() -> Result<Pid, string>,
    restart: RestartType,  // Permanent, Temporary, Transient
    shutdown: int          // Timeout in ms
}

fn supervisor(strategy: RestartStrategy, children: [ChildSpec]) -> Pid {
    spawn(fn() {
        trap_exit(true)
        let pids = start_children(children)
        supervisor_loop(strategy, children, pids)
    })
}
```

### 2.4 Named Processes & Registry

```tofu
// Register process with name
register("worker_pool", pid)

// Look up by name
let pool = whereis("worker_pool")

// Send by name
send_named("worker_pool", message)
```

---

## Phase 3: AI Agent Framework

**Goal:** Build OTP-like behaviors specifically for AI agents.

**Duration:** 3-4 weeks

### 3.1 Agent Behavior

```tofu
// Define an agent behavior (like gen_server)
behavior Agent {
    // Called on start
    fn init(args: any) -> Result<State, string>

    // Handle tool calls
    fn handle_tool(tool: string, args: map<string, any>, state: State) -> {response: any, state: State}

    // Handle LLM responses
    fn handle_inference(response: string, state: State) -> {action: Action, state: State}

    // Handle messages from other agents
    fn handle_message(msg: any, from: Pid, state: State) -> {reply: any, state: State}
}

// Start an agent
let agent = Agent.start(MyAgent, initial_args)

// Call agent (synchronous)
let result = Agent.call(agent, {tool: "search", query: "weather"})

// Cast to agent (asynchronous)
Agent.cast(agent, {type: "update_context", data: new_context})
```

### 3.2 Tool Registry & Dispatch

```tofu
// Tools are automatically registered
@tool(description: "Search the web")
fn search(query: string) -> Result<[SearchResult], string> {
    // implementation
}

// Agent can call tools
fn handle_tool(tool: string, args: map<string, any>, state: State) -> {response: any, state: State} {
    let result = call_tool(tool, args)  // Automatic dispatch
    return {response: result, state: state}
}

// List available tools (for LLM)
let tools = list_tools()  // Returns OpenAI-compatible schema
```

### 3.3 LLM Integration

```tofu
// Built-in LLM client
let response = llm.chat({
    model: "claude-3-opus",
    messages: messages,
    tools: list_tools()
})

// Streaming
llm.stream({...}, fn(chunk) {
    print(chunk)
})

// With automatic tool execution
let result = llm.run_agent({
    model: "claude-3-opus",
    system: "You are a helpful assistant",
    tools: list_tools(),
    max_turns: 10
})
```

### 3.4 Memory & Context Management

```tofu
struct Memory {
    short_term: [Message],      // Recent conversation
    long_term: VectorStore,     // Semantic memory
    working: map<string, any>   // Current task state
}

// Built-in memory operations
memory.add(message)
memory.search("similar context", limit: 5)
memory.summarize()  // Compress old messages
```

---

## Phase 4: Distribution (Multi-Node)

**Goal:** Run agents across multiple machines.

**Duration:** 4-6 weeks

### 4.1 Node Discovery & Connection

```tofu
// Start as distributed node
node.start("agent1@192.168.1.1")

// Connect to another node
node.connect("agent2@192.168.1.2")

// Spawn on remote node
let pid = spawn_on("agent2@192.168.1.2", fn() {
    // runs on remote node
})
```

### 4.2 Global Process Registry

```tofu
// Register globally
global.register("master_agent", pid)

// Lookup across cluster
let master = global.whereis("master_agent")

// Send across nodes
send(master, message)  // Transparent!
```

### 4.3 Agent Migration

```tofu
// Move agent to another node (with state)
migrate(agent_pid, "agent2@192.168.1.2")

// Load balancing
let node = least_loaded_node()
spawn_on(node, worker_fn)
```

---

## Phase 5: Developer Experience

**Goal:** Make Tofulang delightful to use.

**Duration:** Ongoing

### 5.1 REPL

```bash
$ tofu repl
tofu> let x = 1 + 2
3
tofu> spawn(fn() { print("hello") })
<0.42.0>
"hello"
tofu>
```

### 5.2 Hot Code Reloading

```tofu
// Update agent code without stopping
code.load("my_agent.tofu")

// Agents automatically pick up new version
```

### 5.3 Debugger

```tofu
// Trace agent messages
trace.messages(agent_pid)

// Trace tool calls
trace.tools(agent_pid)

// Break on condition
debug.break_on(fn(msg) { msg.type == "error" })
```

### 5.4 Package Manager

```bash
$ tofu pkg install openai-tools
$ tofu pkg install vector-store
```

```tofu
import "openai-tools" as openai
import "vector-store" as vectors
```

---

## Implementation Priority

### Immediate (This Week)
1. [ ] Add `fs.write_bytes()` for binary output
2. [ ] Fix actor runtime initialization (spawn/send/receive)
3. [ ] Write lexer in Tofulang (test self-hosting feasibility)

### Short Term (1 Month)
4. [ ] Complete self-hosting compiler
5. [ ] Expose all actor primitives to language
6. [ ] Add process linking and monitoring
7. [ ] Basic supervisor behavior

### Medium Term (3 Months)
8. [ ] Agent behavior framework
9. [ ] LLM integration (llm.chat, llm.stream)
10. [ ] Tool dispatch system
11. [ ] Memory management
12. [ ] REPL

### Long Term (6+ Months)
13. [ ] Distribution (multi-node)
14. [ ] Hot code reloading
15. [ ] Package manager
16. [ ] Production hardening

---

## File Structure (Target)

```
tofulang/
├── src/
│   ├── lang/           # Lexer, parser, compiler (eventually in Tofulang)
│   ├── vm/             # Virtual machine
│   ├── runtime/        # Scheduler, blocks, mailboxes
│   ├── net/            # HTTP, WebSocket, TCP
│   ├── builtin/        # Tools, inference, memory
│   └── types/          # Value types
├── lib/
│   ├── std/            # Standard library (Tofulang)
│   │   ├── agent.tofu  # Agent behavior
│   │   ├── supervisor.tofu
│   │   ├── llm.tofu    # LLM client
│   │   └── tools.tofu  # Tool utilities
│   └── contrib/        # Community packages
├── examples/
│   ├── chat_agent.tofu
│   ├── research_agent.tofu
│   └── multi_agent.tofu
└── bootstrap/
    ├── lexer.tofu      # Self-hosting compiler
    ├── parser.tofu
    └── compiler.tofu
```

---

## Success Metrics

1. **Self-hosting**: Compiler written in Tofulang
2. **Concurrency**: 1M+ lightweight agents on single machine
3. **Latency**: <1ms message passing between agents
4. **Fault tolerance**: Supervisor restarts in <10ms
5. **Adoption**: Used for production AI agents

---

## Inspiration & References

- **Erlang/OTP**: Process model, supervisors, behaviors
- **Elixir**: Modern syntax, macros, tooling
- **Go**: Goroutines, channels (alternative to mailboxes)
- **Rust**: Result/Option types, pattern matching
- **LangChain/AutoGPT**: Agent patterns, tool calling
