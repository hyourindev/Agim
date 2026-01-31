# CLAUDE.md - Agim Architecture & Design Decisions

This document describes the intentional design decisions in Agim. **Do not suggest changes to these patterns** without understanding why they exist.

## Project Overview

**Agim** is "The Erlang for AI Agents" - a language and runtime for building fault-tolerant, distributed AI agent systems.

- **~45,000 lines of C code**
- **Erlang-inspired** actor model with lightweight processes
- **Built for AI agents**: LLM integration, tool calling, supervisors

## Build & Test

```bash
cd build
cmake ..
make -j4
ctest --output-on-failure
```

## Directory Structure

```
src/
├── vm/        # Virtual machine, GC, value representation, sandbox
├── lang/      # Lexer, parser, compiler, type checker
├── runtime/   # Scheduler, blocks (processes), mailboxes, workers, supervisors
├── types/     # Arrays, maps, strings, vectors, closures
├── dist/      # Multi-node distribution (WIP)
├── builtin/   # Built-in tools, inference callbacks
├── debug/     # Tracing, telemetry
└── util/      # Memory allocation, hashing, pools
```

---

## Critical Design Decisions (DO NOT CHANGE)

### 1. Threading Model: Actor-Based Concurrency

**Design**: Erlang-style lightweight processes called "Blocks"

- Each Block has its own VM, heap, mailbox, and capabilities
- Blocks communicate via async message passing only
- No shared memory between blocks (isolation by design)
- PIDs are 64-bit integers

**Why**: Isolation prevents cascading failures. One block crashing doesn't corrupt another's heap.

**DO NOT**:
- Add shared memory or global state between blocks
- Add synchronous message passing primitives
- Remove the per-block heap isolation

### 2. Preemptive Scheduling via Reductions

**Design**: Each block gets N reductions (instruction count) before yielding

```c
vm->reduction_limit = 10000;  // Default
```

**Why**: Prevents any single block from starving others. Essential for fairness in multi-agent systems.

**DO NOT**:
- Remove reduction counting
- Make reduction limits configurable per-instruction (too complex)
- Allow blocks to disable preemption

### 3. Per-Block Heaps with Generational GC

**Design**: Each block has its own heap with:
- Mark-sweep collector
- Incremental marking (gray list)
- Generational collection (young/old)
- Write barriers (remember set)

**Why**:
- GC in one block doesn't pause others
- Enables parallel minor GCs
- No global GC pauses

Note: Card table was removed as dead code. The remember set tracks old-to-young references directly.

**DO NOT**:
- Add a global heap
- Share objects between heaps without copying
- Remove write barriers (breaks generational GC)

### 4. NaN-Boxing Value Representation

**Design**: All values packed into 64-bit using IEEE 754 quiet NaN space

```c
// Doubles: normal IEEE 754 values
// Integers: QNAN_TAG | (48-bit signed int)
// Objects: QNAN_TAG | (48-bit pointer)
// PIDs: QNAN_TAG | (48-bit process ID)
```

**Why**:
- No heap allocation for primitives
- Single comparison for type checking
- Cache-friendly representation
- x86-64 canonical addresses fit in 48 bits

**DO NOT**:
- Change the NaN-boxing scheme (breaks all type checks)
- Add value types that don't fit the scheme
- Use tagged pointers instead

### 5. Lock-Free MPSC Mailboxes

**Design**: Dmitry Vyukov's MPSC queue with stub node

- Multiple producers (senders), single consumer (receiver)
- Atomic operations for ordering
- Condition variables for blocking receive

**Why**: High-throughput message passing without lock contention.

**DO NOT**:
- Add locks to the hot path
- Change to MPMC (changes semantics)
- Remove the stub node pattern (breaks empty state)

### 6. Capability-Based Security (Deny by Default)

**Design**: 17 capabilities as a 32-bit bitmask per block

```c
CAP_NONE = 0  // Default - no permissions
CAP_ALL = 0xFFFFFFFF  // Full access
```

Capabilities: `SPAWN`, `SEND`, `RECEIVE`, `FILE_READ`, `FILE_WRITE`, `EXEC`, `SHELL`, etc.

**Why**: Untrusted code can run with minimal permissions. Explicit capability grants.

**DO NOT**:
- Make CAP_ALL the default (security hole)
- Add capabilities that bypass the sandbox
- Allow capability revocation (not supported)

### 7. Sandbox for File Access

**Design**:
- Path whitelist for read/write directories
- Canonicalization to prevent traversal (../ attacks)
- Default: restrictive (no file access)

**Why**: Prevents untrusted code from reading/writing arbitrary files.

**DO NOT**:
- Make the sandbox permissive by default
- Skip path canonicalization
- Allow symlink following outside sandbox

### 8. Work-Stealing Scheduler

**Design**:
- Per-worker deques (Chase-Lev algorithm)
- Idle workers steal from busy workers
- Global run queue as fallback

**Why**: Automatic load balancing without central coordination.

**DO NOT**:
- Remove work stealing
- Use a single global queue (contention)
- Add complex priority schemes

### 9. Timer Wheel with Global Lock

**Design**: Hierarchical timer wheel with O(1) add/cancel

- Per-bucket locks exist but global lock is used
- Atomic min_deadline for efficient polling

**Why**: Timer operations are infrequent; global lock is simpler and correct. The complexity of per-bucket locking for timer_add (which needs free_list access) isn't worth the marginal benefit.

**DO NOT**:
- Attempt per-bucket locking without restructuring free_list
- Remove the min_deadline atomic (used for efficient polling)

### 10. GC Size Estimates (Base Sizes Only)

**Design**: `value_size()` returns base allocation sizes without padding

```c
VAL_STRING: sizeof(Value) + sizeof(String)  // No +64 padding
VAL_ARRAY:  sizeof(Value) + sizeof(Array)   // No +8*sizeof(Value*)
```

**Why**: Conservative padding caused 4-8x more GC triggers. Actual sizes are tracked at allocation.

**DO NOT**:
- Add back conservative padding
- Use value_size() for precise allocation tracking (it's an estimate)

### 11. String Interning Cache (1024 Sets)

**Design**: 4-way set-associative cache with 1024 sets = 4096 entries

**Why**: Larger cache = better hit rate for common strings.

**DO NOT**:
- Reduce cache size
- Remove interning (increases allocations)

### 12. Spin Thresholds

**Design**:
- Mailbox: 100 spins before yielding
- Worker: 20 spins before sleeping

**Why**: Balance between latency (spin) and CPU usage (sleep). High spin counts waste CPU.

**DO NOT**:
- Increase spin counts significantly (wastes CPU)
- Remove spinning entirely (hurts latency)

### 13. Block Link/Monitor Mutex

**Design**: Per-block mutex (`link_mutex`) protects link/monitor arrays

```c
pthread_mutex_t link_mutex;  // Protects links, monitors, monitored_by
```

**Why**: Multiple threads may concurrently link/unlink blocks during exit propagation.

**DO NOT**:
- Remove the mutex (causes data races)
- Use a global lock (reduces parallelism)
- Hold the lock while calling external functions

### 14. Run Queue Synchronization

**Design**: Mutex on RunQueue, used only in multi-threaded mode

```c
if (scheduler->worker_count > 0) {
    pthread_mutex_lock(&scheduler->run_queue.lock);
    // ... queue operation ...
    pthread_mutex_unlock(&scheduler->run_queue.lock);
}
```

**Why**: Global run queue can be accessed from multiple workers. Single-threaded mode skips locking for performance.

**DO NOT**:
- Use lock in single-threaded mode (unnecessary overhead)
- Access run queue without lock in multi-threaded mode

### 15. Block Registry Atomicity

**Design**: CAS loop for max_blocks check to prevent TOCTOU race

```c
while (current < max) {
    if (atomic_compare_exchange_weak(&registry.total_count, &current, current + 1)) {
        // Reserved slot, now insert
        break;
    }
}
```

**Why**: Prevents spawning more blocks than max_blocks limit under concurrent spawn requests.

**DO NOT**:
- Use separate check-then-increment (TOCTOU race)
- Remove the max_blocks enforcement

### 16. Worker In-Flight Tracking

**Design**: Atomic counter tracks blocks currently being executed

```c
_Atomic(size_t) blocks_in_flight;  // In Scheduler

// In worker loop:
atomic_fetch_add(&scheduler->blocks_in_flight, 1);
// ... execute block ...
atomic_fetch_sub(&scheduler->blocks_in_flight, 1);
```

**Why**: Prevents premature worker termination while blocks are still executing.

**DO NOT**:
- Remove in-flight tracking (causes early termination)
- Check only spawned vs terminated (misses executing blocks)

### 17. Lazy Register Initialization

**Design**: Only initialize registers that will be used in function calls

```c
uint8_t max_reg = target_chunk->num_regs > 0 ? target_chunk->num_regs : 16;
for (int j = 0; j <= max_reg && j < REG_MAX_REGISTERS; j++) {
    new_frame->regs[j] = NANBOX_NIL;
}
```

**Why**: Reduces per-call overhead from 2KB to typically <256 bytes.

**DO NOT**:
- Initialize all 256 registers unconditionally
- Skip initialization entirely (undefined behavior)

### 18. Inline Cache Hash Function

**Design**: Multiplicative hash using Knuth's golden ratio

```c
static inline size_t ic_hash(uintptr_t shape) {
    uint64_t h = (uint64_t)shape * 0x9E3779B97F4A7C15ULL;
    return (size_t)((h >> 61) ^ (h & (IC_MAX_ENTRIES - 1)));
}
```

**Why**: Better distribution than simple right-shift for 8-slot cache.

**DO NOT**:
- Use simple modulo (poor distribution)
- Use expensive hash functions (hot path)

---

## Intentional Constraints (Features NOT Included)

### No Global Heap
Each block has its own heap. This is intentional for isolation.

### No Synchronous Message Passing
Only async `send()`/`receive()`. RPC patterns are built on top.

### No Exceptions
Use `Result<T, E>` and `Option<T>` types. Explicit error handling.

### No Hot Code Reloading (Yet)
Fields reserved (`module_name`, `pending_upgrade`) but not implemented.

### No GC Tuning from User Code
GC config set at block creation. No `GC.collect()` calls.

### No Language-Level Macros
`@tool` decorator is simple attribute syntax, not code generation.

---

## Safe to Modify

- Test suite (`tests/`)
- Example programs (`examples/`)
- Documentation and comments
- Non-critical optimizations
- Adding new opcodes (follow existing patterns)

## Requires Careful Review

- VM instruction dispatch
- Value representation (NaN-boxing)
- Scheduler/worker code
- GC write barriers
- Capability checks
- Mailbox implementation
- Block registry
- Block link/monitor synchronization

---

## Common "Issues" That Are Actually Intentional

### "Timer uses global lock instead of per-bucket locks"
**Intentional**: Per-bucket locking requires restructuring free_list. Global lock is simple and timers aren't a bottleneck. Note: Per-bucket locks were removed as dead code since the global lock is always used.

### "Heap counters aren't atomic"
**Intentional**: Each heap is accessed by a single thread via TLS. Atomics would add overhead with no benefit.

### "scheduler_spawn uses CAP_NONE by default"
**Intentional**: Secure by default. Use `scheduler_spawn_ex()` with explicit capabilities.

### "sandbox_global() returns restrictive sandbox"
**Intentional**: Deny by default. Configure allowed paths explicitly.

### "Value creation functions return NULL on allocation failure"
**Intentional**: Callers must handle OOM. No hidden exceptions or aborts.

### "String concatenation allocates new string"
**Intentional**: Strings are immutable. COW would complicate the implementation.

### "No mutex around heap->bytes_allocated"
**Intentional**: Per-block heaps are single-threaded.

### "messages_received counter is atomic but messages_sent is not"
**Intentional**: `messages_received` is updated by multiple sender threads. `messages_sent` is only updated by the owning block.

### "Block link/monitor functions acquire mutex"
**Intentional**: These can be called from multiple threads during exit propagation and supervisor operations.

### "Card table removed from GC"
**Intentional**: The card table was allocated and written to but never scanned during collection. The remember set provides the same functionality.

### "Timer bucket locks removed"
**Intentional**: Per-bucket locks were never used; the global lock handles all timer operations. Removing them saves memory and simplifies the code.

---

## Performance Characteristics

- **Message passing**: <1ms between blocks
- **Block creation**: ~1000 blocks/ms
- **GC pause**: <1ms (incremental)
- **Reductions**: 10,000 instructions per timeslice
- **Work stealing**: Random victim selection

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────┐
│                      Scheduler                          │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐   │
│  │ Worker  │  │ Worker  │  │ Worker  │  │ Worker  │   │
│  │ Deque   │  │ Deque   │  │ Deque   │  │ Deque   │   │
│  └────┬────┘  └────┬────┘  └────┬────┘  └────┬────┘   │
│       │ steal      │ steal      │ steal      │         │
│       └────────────┴────────────┴────────────┘         │
└─────────────────────────────────────────────────────────┘
                           │
          ┌────────────────┼────────────────┐
          ▼                ▼                ▼
    ┌──────────┐    ┌──────────┐    ┌──────────┐
    │  Block   │    │  Block   │    │  Block   │
    │ ┌──────┐ │    │ ┌──────┐ │    │ ┌──────┐ │
    │ │  VM  │ │    │ │  VM  │ │    │ │  VM  │ │
    │ ├──────┤ │    │ ├──────┤ │    │ ├──────┤ │
    │ │ Heap │ │    │ │ Heap │ │    │ │ Heap │ │
    │ ├──────┤ │    │ ├──────┤ │    │ ├──────┤ │
    │ │Mailbox│◄────┤ │Mailbox│◄────┤ │Mailbox│ │
    │ ├──────┤ │    │ ├──────┤ │    │ ├──────┤ │
    │ │ Caps │ │    │ │ Caps │ │    │ │ Caps │ │
    │ └──────┘ │    │ └──────┘ │    │ └──────┘ │
    └──────────┘    └──────────┘    └──────────┘
```

---

## References

- **Erlang/OTP**: Process model, supervisors, "let it crash"
- **Vyukov MPSC**: Lock-free queue design
- **Chase-Lev**: Work-stealing deque
- **NaN-boxing**: LuaJIT, SpiderMonkey value representation
- **Generational GC**: V8, .NET GC design
