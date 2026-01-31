# Agim Production Readiness Plan - Mission Critical

**Version:** 1.0
**Target:** Mission-Critical Production Deployment
**Estimated Effort:** 6-12 months with dedicated team

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Phase 1: Testing Infrastructure](#phase-1-testing-infrastructure)
3. [Phase 2: Security Hardening](#phase-2-security-hardening)
4. [Phase 3: Reliability Engineering](#phase-3-reliability-engineering)
5. [Phase 4: Performance Engineering](#phase-4-performance-engineering)
6. [Phase 5: Observability](#phase-5-observability)
7. [Phase 6: Documentation](#phase-6-documentation)
8. [Phase 7: Operations](#phase-7-operations)
9. [Phase 8: Compliance](#phase-8-compliance)
10. [Phase 9: Feature Completion](#phase-9-feature-completion)
11. [Phase 10: Production Burn-in](#phase-10-production-burn-in)
12. [Detailed Task Breakdown](#detailed-task-breakdown)
13. [Risk Register](#risk-register)
14. [Success Criteria](#success-criteria)

---

## Executive Summary

This document outlines the complete path to making Agim mission-critical production ready. The plan is organized into 10 phases with detailed macro and micro tasks. Each task includes acceptance criteria and dependencies.

### Current State Assessment

| Area | Current | Target | Gap |
|------|---------|--------|-----|
| Test Coverage | ~60% estimated | 95%+ | Significant |
| Security Testing | Basic | Comprehensive | Critical |
| Fuzzing | None | All parsers | Critical |
| Thread Safety | Partial | Verified | High |
| Documentation | Architecture only | Complete | Medium |
| Observability | Basic tracing | Full stack | High |
| Operations | Manual | Automated | High |

---

## Phase 1: Testing Infrastructure

### 1.1 Macro Goals
- Achieve 95%+ code coverage
- Implement all sanitizer testing
- Establish fuzz testing for all input handlers
- Create comprehensive benchmark suite
- Implement property-based testing

### 1.2 Detailed Tasks

#### 1.1.1 Unit Test Coverage Expansion

##### 1.1.1.1 VM Unit Tests
- [x] **test_vm_stack_operations.c** - All stack operations
  - [x] Test OP_PUSH with all value types
  - [x] Test OP_POP edge cases (empty stack)
  - [x] Test OP_DUP with reference counting
  - [x] Test OP_SWAP boundary conditions
  - [x] Test stack overflow detection
  - [x] Test stack underflow detection
  - [x] Verify stack alignment after operations

- [ ] **test_vm_arithmetic.c** - Arithmetic operations
  - [ ] Test OP_ADD integer overflow
  - [ ] Test OP_ADD float precision
  - [ ] Test OP_ADD mixed int/float
  - [ ] Test OP_SUB underflow
  - [ ] Test OP_MUL overflow
  - [ ] Test OP_DIV by zero (int)
  - [ ] Test OP_DIV by zero (float) - should produce Inf
  - [ ] Test OP_MOD with negative numbers
  - [ ] Test OP_MOD by zero
  - [ ] Test OP_NEG with MIN_INT
  - [ ] Test NaN propagation

- [ ] **test_vm_comparison.c** - Comparison operations
  - [ ] Test OP_EQ with all type combinations
  - [ ] Test OP_EQ reference equality for objects
  - [ ] Test OP_LT with strings (lexicographic)
  - [ ] Test OP_LT with mixed numeric types
  - [ ] Test OP_LE boundary cases
  - [ ] Test comparison with NaN values
  - [ ] Test comparison with Inf values

- [ ] **test_vm_control_flow.c** - Control flow
  - [ ] Test OP_JMP forward
  - [ ] Test OP_JMP backward
  - [ ] Test OP_JMP to end of code
  - [ ] Test OP_JMP_IF with truthy values
  - [ ] Test OP_JMP_IF with falsy values
  - [ ] Test OP_JMP_UNLESS inverse
  - [ ] Test OP_LOOP iteration limits
  - [ ] Test nested loops
  - [ ] Test break/continue semantics

- [ ] **test_vm_functions.c** - Function calls
  - [ ] Test OP_CALL with 0 arguments
  - [ ] Test OP_CALL with max arguments
  - [ ] Test OP_CALL stack frame setup
  - [ ] Test OP_RET value propagation
  - [ ] Test OP_RET void functions
  - [ ] Test recursive calls up to limit
  - [ ] Test tail call optimization
  - [ ] Test closure capture
  - [ ] Test upvalue handling

- [ ] **test_vm_memory.c** - Memory operations
  - [ ] Test OP_ARRAY_NEW allocation
  - [ ] Test OP_ARRAY_PUSH growth
  - [ ] Test OP_ARRAY_GET bounds checking
  - [ ] Test OP_ARRAY_SET bounds checking
  - [ ] Test OP_MAP_NEW allocation
  - [ ] Test OP_MAP_GET missing key
  - [ ] Test OP_MAP_SET overwrite
  - [ ] Test OP_MAP_DELETE
  - [ ] Test string interning
  - [ ] Test string concatenation

- [ ] **test_vm_process.c** - Process operations
  - [ ] Test OP_SPAWN capability check
  - [ ] Test OP_SEND to valid PID
  - [ ] Test OP_SEND to invalid PID
  - [ ] Test OP_SEND to dead process
  - [ ] Test OP_RECEIVE with message
  - [ ] Test OP_RECEIVE without message (blocks)
  - [ ] Test OP_RECEIVE with timeout
  - [ ] Test OP_SELF returns correct PID
  - [ ] Test OP_YIELD reduction counting

##### 1.1.1.2 GC Unit Tests
- [ ] **test_gc_allocation.c** - Allocation
  - [ ] Test heap_alloc returns valid pointer
  - [ ] Test heap_alloc fails at max_size
  - [ ] Test heap_alloc triggers GC at threshold
  - [ ] Test heap_alloc_with_gc behavior
  - [ ] Test allocation of each value type
  - [ ] Test allocation alignment
  - [ ] Test allocation size tracking

- [ ] **test_gc_marking.c** - Mark phase
  - [ ] Test gc_mark_value sets mark bit
  - [ ] Test gc_mark_value traverses arrays
  - [ ] Test gc_mark_value traverses maps
  - [ ] Test gc_mark_value traverses closures
  - [ ] Test gc_mark_roots marks stack
  - [ ] Test gc_mark_roots marks globals
  - [ ] Test gc_mark_roots marks upvalues
  - [ ] Test gc_mark_roots marks constants
  - [ ] Test gray list operations
  - [ ] Test incremental marking work packets

- [ ] **test_gc_sweeping.c** - Sweep phase
  - [ ] Test unmarked objects freed
  - [ ] Test marked objects preserved
  - [ ] Test mark bit cleared after sweep
  - [ ] Test bytes_allocated updated
  - [ ] Test object list maintained
  - [ ] Test sweep handles cycles
  - [ ] Test incremental sweeping

- [ ] **test_gc_generational.c** - Generational GC
  - [ ] Test young generation collection
  - [ ] Test promotion to old generation
  - [ ] Test promotion_threshold
  - [ ] Test write barrier triggers
  - [ ] Test remember set population
  - [ ] Test remember set clearing
  - [ ] Test full collection
  - [ ] Test needs_full_gc flag
  - [ ] Test young_gc_threshold adjustment

- [ ] **test_gc_concurrent.c** - Concurrent safety
  - [ ] Test refcount atomic operations
  - [ ] Test REFCOUNT_FREEING sentinel
  - [ ] Test value_retain during sweep
  - [ ] Test value_release races
  - [ ] Test COW during GC

##### 1.1.1.3 Scheduler Unit Tests
- [ ] **test_scheduler_lifecycle.c** - Lifecycle
  - [ ] Test scheduler_new with config
  - [ ] Test scheduler_new default config
  - [ ] Test scheduler_free cleans up
  - [ ] Test scheduler_free with active blocks
  - [ ] Test scheduler_free with workers

- [ ] **test_scheduler_spawn.c** - Spawning
  - [ ] Test scheduler_spawn returns valid PID
  - [ ] Test scheduler_spawn increments next_pid
  - [ ] Test scheduler_spawn registers block
  - [ ] Test scheduler_spawn_ex with capabilities
  - [ ] Test scheduler_spawn_ex with limits
  - [ ] Test spawn at max_blocks fails
  - [ ] Test spawn with invalid bytecode

- [ ] **test_scheduler_registry.c** - Registry
  - [ ] Test registry_insert
  - [ ] Test registry_lookup found
  - [ ] Test registry_lookup not found
  - [ ] Test registry_remove
  - [ ] Test registry sharding distribution
  - [ ] Test registry growth
  - [ ] Test registry concurrent access

- [ ] **test_scheduler_runqueue.c** - Run queue
  - [ ] Test runqueue_push adds to tail
  - [ ] Test runqueue_pop removes from head
  - [ ] Test runqueue_pop empty queue
  - [ ] Test runqueue_remove middle element
  - [ ] Test runqueue count accuracy
  - [ ] Test runqueue with mutex

- [ ] **test_scheduler_execution.c** - Execution
  - [ ] Test scheduler_step executes one block
  - [ ] Test scheduler_step handles yield
  - [ ] Test scheduler_step handles waiting
  - [ ] Test scheduler_step handles completion
  - [ ] Test scheduler_step handles error
  - [ ] Test scheduler_run terminates
  - [ ] Test scheduler_stop interrupts
  - [ ] Test context switch counting

- [ ] **test_scheduler_exit.c** - Exit propagation
  - [ ] Test normal exit propagation
  - [ ] Test crash exit propagation
  - [ ] Test linked process notification
  - [ ] Test monitor notification
  - [ ] Test trap_exit handling
  - [ ] Test supervisor notification
  - [ ] Test cascading crashes

##### 1.1.1.4 Block Unit Tests
- [ ] **test_block_lifecycle.c** - Lifecycle
  - [ ] Test block_new allocation
  - [ ] Test block_new with limits
  - [ ] Test block_free cleanup
  - [ ] Test block_load bytecode
  - [ ] Test block state transitions

- [ ] **test_block_capabilities.c** - Capabilities
  - [ ] Test block_grant adds caps
  - [ ] Test block_revoke removes caps
  - [ ] Test block_has_cap checks
  - [ ] Test block_check_cap crashes on deny
  - [ ] Test CAP_NONE default
  - [ ] Test each capability individually

- [ ] **test_block_linking.c** - Linking
  - [ ] Test block_link adds link
  - [ ] Test block_link idempotent
  - [ ] Test block_unlink removes link
  - [ ] Test block_unlink missing no-op
  - [ ] Test block_get_links returns copy
  - [ ] Test link array growth
  - [ ] Test link mutex contention

- [ ] **test_block_monitoring.c** - Monitoring
  - [ ] Test block_monitor adds monitor
  - [ ] Test block_demonitor removes monitor
  - [ ] Test block_add_monitored_by
  - [ ] Test block_remove_monitored_by
  - [ ] Test monitor array growth
  - [ ] Test bidirectional relationship

- [ ] **test_block_messaging.c** - Messaging
  - [ ] Test block_send to live block
  - [ ] Test block_send to dead block
  - [ ] Test block_send COW for arrays
  - [ ] Test block_send COW for maps
  - [ ] Test block_send copies closures
  - [ ] Test block_receive pops message
  - [ ] Test block_receive empty returns NULL
  - [ ] Test block_has_messages

##### 1.1.1.5 Worker Unit Tests
- [ ] **test_worker_deque.c** - Chase-Lev deque
  - [ ] Test deque_init
  - [ ] Test deque_push single item
  - [ ] Test deque_push multiple items
  - [ ] Test deque_push triggers growth
  - [ ] Test deque_pop LIFO order
  - [ ] Test deque_pop empty returns NULL
  - [ ] Test deque_steal FIFO order
  - [ ] Test deque_steal empty returns NULL
  - [ ] Test deque_steal concurrent with pop
  - [ ] Test deque_size accuracy
  - [ ] Test retired buffer cleanup

- [ ] **test_worker_lifecycle.c** - Lifecycle
  - [ ] Test worker_new allocation
  - [ ] Test worker_free cleanup
  - [ ] Test worker_start spawns thread
  - [ ] Test worker_stop signals stop
  - [ ] Test worker_join waits

- [ ] **test_worker_stealing.c** - Work stealing
  - [ ] Test worker_steal from other worker
  - [ ] Test worker_steal random victim
  - [ ] Test worker_steal skips self
  - [ ] Test worker_steal empty victims
  - [ ] Test steal statistics

- [ ] **test_worker_execution.c** - Execution
  - [ ] Test worker executes block
  - [ ] Test worker handles yield
  - [ ] Test worker handles completion
  - [ ] Test worker termination check
  - [ ] Test worker backoff on idle

##### 1.1.1.6 Mailbox Unit Tests
- [ ] **test_mailbox_mpsc.c** - MPSC queue
  - [ ] Test mailbox_init
  - [ ] Test mailbox_push single
  - [ ] Test mailbox_push multiple
  - [ ] Test mailbox_push at limit
  - [ ] Test mailbox_pop single
  - [ ] Test mailbox_pop order (FIFO)
  - [ ] Test mailbox_pop empty
  - [ ] Test mailbox_empty
  - [ ] Test mailbox_free cleanup

- [ ] **test_mailbox_concurrent.c** - Concurrent access
  - [ ] Test multiple producers single consumer
  - [ ] Test producer consumer interleaving
  - [ ] Test stub node handling
  - [ ] Test atomic ordering

##### 1.1.1.7 Timer Unit Tests
- [ ] **test_timer_wheel.c** - Timer wheel
  - [ ] Test timer_wheel_new
  - [ ] Test timer_wheel_free
  - [ ] Test timer_add single timer
  - [ ] Test timer_add multiple timers
  - [ ] Test timer_add same slot
  - [ ] Test timer_cancel before fire
  - [ ] Test timer_cancel after fire no-op
  - [ ] Test timer_tick fires expired
  - [ ] Test timer_tick requeues not-expired
  - [ ] Test timer_next_deadline
  - [ ] Test timer_has_pending
  - [ ] Test deadline overflow handling
  - [ ] Test min_deadline tracking

##### 1.1.1.8 Value Type Unit Tests
- [ ] **test_value_creation.c** - Creation
  - [ ] Test value_nil
  - [ ] Test value_bool true/false
  - [ ] Test value_int range
  - [ ] Test value_float precision
  - [ ] Test value_string empty
  - [ ] Test value_string_n with length
  - [ ] Test value_array empty
  - [ ] Test value_map empty
  - [ ] Test value_pid
  - [ ] Test value_function
  - [ ] Test value_bytes
  - [ ] Test value_vector

- [ ] **test_value_refcount.c** - Reference counting
  - [ ] Test value_retain increments
  - [ ] Test value_release decrements
  - [ ] Test value_release frees at zero
  - [ ] Test value_retain during free fails
  - [ ] Test atomic operations

- [ ] **test_value_cow.c** - Copy-on-write
  - [ ] Test value_cow_share for arrays
  - [ ] Test value_cow_share for maps
  - [ ] Test COW flag propagation
  - [ ] Test copy on mutation
  - [ ] Test cow_detach

- [ ] **test_value_nanbox.c** - NaN-boxing
  - [ ] Test nanbox_int range
  - [ ] Test nanbox_double values
  - [ ] Test nanbox_bool
  - [ ] Test nanbox_nil
  - [ ] Test nanbox_pid
  - [ ] Test nanbox_obj pointer
  - [ ] Test nanbox type checks
  - [ ] Test nanbox extraction

##### 1.1.1.9 Array Unit Tests
- [ ] **test_array_operations.c** - Operations
  - [ ] Test array_push grows capacity
  - [ ] Test array_push returns new array
  - [ ] Test array_get in bounds
  - [ ] Test array_get out of bounds
  - [ ] Test array_set in bounds
  - [ ] Test array_set out of bounds
  - [ ] Test array_length
  - [ ] Test array_pop
  - [ ] Test array_slice
  - [ ] Test array_concat
  - [ ] Test array iteration

##### 1.1.1.10 Map Unit Tests
- [ ] **test_map_operations.c** - Operations
  - [ ] Test map_set new key
  - [ ] Test map_set overwrite key
  - [ ] Test map_get existing key
  - [ ] Test map_get missing key
  - [ ] Test map_delete existing
  - [ ] Test map_delete missing
  - [ ] Test map_size
  - [ ] Test map_has
  - [ ] Test map growth
  - [ ] Test map iteration
  - [ ] Test hash collision handling

##### 1.1.1.11 String Unit Tests
- [ ] **test_string_operations.c** - Operations
  - [ ] Test string_length
  - [ ] Test string_compare equal
  - [ ] Test string_compare less
  - [ ] Test string_compare greater
  - [ ] Test string_concat
  - [ ] Test string_slice
  - [ ] Test string_find
  - [ ] Test string_split
  - [ ] Test string_trim
  - [ ] Test string_upper
  - [ ] Test string_lower

- [ ] **test_string_interning.c** - Interning
  - [ ] Test string_intern caches
  - [ ] Test string_intern returns same
  - [ ] Test string_intern eviction
  - [ ] Test string_intern thread safety

##### 1.1.1.12 Closure Unit Tests
- [ ] **test_closure_operations.c** - Operations
  - [ ] Test closure_new
  - [ ] Test closure upvalue capture
  - [ ] Test upvalue_new open
  - [ ] Test upvalue_close
  - [ ] Test upvalue_is_open
  - [ ] Test closure_free

##### 1.1.1.13 File I/O Unit Tests
- [ ] **test_file_operations.c** - File I/O
  - [ ] Test fs_read success
  - [ ] Test fs_read nonexistent
  - [ ] Test fs_write success
  - [ ] Test fs_write permissions
  - [ ] Test fs_exists
  - [ ] Test fs_lines

##### 1.1.1.14 Compiler Unit Tests
- [ ] **test_lexer_tokens.c** - Lexer
  - [ ] Test all keyword tokens
  - [ ] Test identifier tokens
  - [ ] Test integer literals
  - [ ] Test float literals
  - [ ] Test string literals
  - [ ] Test string escapes
  - [ ] Test operators
  - [ ] Test delimiters
  - [ ] Test comments
  - [ ] Test whitespace handling
  - [ ] Test error recovery
  - [ ] Test line/column tracking

- [ ] **test_parser_ast.c** - Parser
  - [ ] Test parse expressions
  - [ ] Test parse statements
  - [ ] Test parse functions
  - [ ] Test parse blocks
  - [ ] Test parse if/else
  - [ ] Test parse loops
  - [ ] Test parse match
  - [ ] Test parse types
  - [ ] Test parse decorators
  - [ ] Test error messages

- [ ] **test_compiler_codegen.c** - Code generation
  - [ ] Test compile literals
  - [ ] Test compile variables
  - [ ] Test compile arithmetic
  - [ ] Test compile comparison
  - [ ] Test compile logical
  - [ ] Test compile if/else
  - [ ] Test compile loops
  - [ ] Test compile functions
  - [ ] Test compile closures
  - [ ] Test compile spawn/send/receive

- [ ] **test_typechecker.c** - Type checker
  - [ ] Test type inference
  - [ ] Test type errors
  - [ ] Test function types
  - [ ] Test generic types
  - [ ] Test union types
  - [ ] Test result types

##### 1.1.1.15 Supervisor Unit Tests
- [ ] **test_supervisor_strategies.c** - Strategies
  - [ ] Test one_for_one restart
  - [ ] Test one_for_all restart
  - [ ] Test rest_for_one restart
  - [ ] Test max_restarts limit
  - [ ] Test max_seconds window
  - [ ] Test permanent child
  - [ ] Test temporary child
  - [ ] Test transient child

##### 1.1.1.16 Sandbox Unit Tests
- [ ] **test_sandbox_paths.c** - Path validation
  - [ ] Test allowed read paths
  - [ ] Test allowed write paths
  - [ ] Test path canonicalization
  - [ ] Test traversal prevention
  - [ ] Test symlink handling
  - [ ] Test relative path rejection

##### 1.1.1.17 Capability Unit Tests
- [ ] **test_capabilities.c** - All capabilities
  - [ ] Test CAP_SPAWN
  - [ ] Test CAP_SEND
  - [ ] Test CAP_RECEIVE
  - [ ] Test CAP_FILE_READ
  - [ ] Test CAP_FILE_WRITE
  - [ ] Test CAP_EXEC
  - [ ] Test CAP_TRAP_EXIT
  - [ ] Test capability inheritance
  - [ ] Test capability checking

#### 1.1.2 Integration Tests

##### 1.1.2.1 Multi-Block Integration
- [ ] **test_integration_messaging.c**
  - [ ] Test send between two blocks
  - [ ] Test send to multiple receivers
  - [ ] Test request-response pattern
  - [ ] Test broadcast pattern
  - [ ] Test message ordering
  - [ ] Test message delivery under load

- [ ] **test_integration_linking.c**
  - [ ] Test bidirectional linking
  - [ ] Test crash propagation
  - [ ] Test unlink before crash
  - [ ] Test trap_exit integration
  - [ ] Test complex link topologies

- [ ] **test_integration_monitoring.c**
  - [ ] Test monitor receives down
  - [ ] Test demonitor stops notifications
  - [ ] Test multiple monitors
  - [ ] Test monitor self

- [ ] **test_integration_supervision.c**
  - [ ] Test supervisor restarts child
  - [ ] Test supervisor restart limit
  - [ ] Test supervisor shutdown
  - [ ] Test nested supervisors
  - [ ] Test supervision tree

##### 1.1.2.2 Scheduler Integration
- [ ] **test_integration_scheduling.c**
  - [ ] Test fair scheduling
  - [ ] Test reduction preemption
  - [ ] Test waiting block wake
  - [ ] Test priority inversion absence
  - [ ] Test starvation prevention

- [ ] **test_integration_workers.c**
  - [ ] Test multi-worker execution
  - [ ] Test work stealing balance
  - [ ] Test worker failure recovery
  - [ ] Test graceful shutdown

#### 1.1.3 End-to-End Tests

##### 1.1.3.1 Application Scenarios
- [ ] **test_e2e_echo_server.c**
  - [ ] Test echo server handles connections
  - [ ] Test echo server concurrent clients
  - [ ] Test echo server graceful shutdown

- [ ] **test_e2e_worker_pool.c**
  - [ ] Test worker pool distributes work
  - [ ] Test worker pool handles failures
  - [ ] Test worker pool scales

- [ ] **test_e2e_pubsub.c**
  - [ ] Test publish to subscribers
  - [ ] Test subscribe/unsubscribe
  - [ ] Test topic filtering

- [ ] **test_e2e_state_machine.c**
  - [ ] Test state transitions
  - [ ] Test state persistence
  - [ ] Test state recovery

- [ ] **test_e2e_pipeline.c**
  - [ ] Test data pipeline stages
  - [ ] Test backpressure handling
  - [ ] Test pipeline failure recovery

#### 1.1.4 Sanitizer Testing

##### 1.1.4.1 AddressSanitizer (ASan)
- [ ] **CMake configuration**
  ```cmake
  option(SANITIZE_ADDRESS "Enable AddressSanitizer" OFF)
  if(SANITIZE_ADDRESS)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
  endif()
  ```
- [ ] Create CI job for ASan
- [ ] Fix all ASan findings
- [ ] Document ASan usage

##### 1.1.4.2 ThreadSanitizer (TSan)
- [ ] **CMake configuration**
  ```cmake
  option(SANITIZE_THREAD "Enable ThreadSanitizer" OFF)
  if(SANITIZE_THREAD)
    add_compile_options(-fsanitize=thread)
    add_link_options(-fsanitize=thread)
  endif()
  ```
- [ ] Create CI job for TSan
- [ ] Create TSan suppression file for known benign races
- [ ] Fix all TSan findings
- [ ] Run TSan on all multi-threaded tests
- [ ] Run TSan on worker tests specifically
- [ ] Run TSan on mailbox tests
- [ ] Document TSan usage

##### 1.1.4.3 MemorySanitizer (MSan)
- [ ] **CMake configuration**
  ```cmake
  option(SANITIZE_MEMORY "Enable MemorySanitizer" OFF)
  if(SANITIZE_MEMORY)
    add_compile_options(-fsanitize=memory -fno-omit-frame-pointer)
    add_link_options(-fsanitize=memory)
  endif()
  ```
- [ ] Create CI job for MSan
- [ ] Fix all MSan findings
- [ ] Document MSan usage

##### 1.1.4.4 UndefinedBehaviorSanitizer (UBSan)
- [ ] **CMake configuration**
  ```cmake
  option(SANITIZE_UNDEFINED "Enable UBSan" OFF)
  if(SANITIZE_UNDEFINED)
    add_compile_options(-fsanitize=undefined)
    add_link_options(-fsanitize=undefined)
  endif()
  ```
- [ ] Create CI job for UBSan
- [ ] Fix all UBSan findings
- [ ] Document UBSan usage

#### 1.1.5 Fuzz Testing

##### 1.1.5.1 Fuzzing Infrastructure
- [ ] Set up libFuzzer integration
- [ ] Set up AFL++ integration
- [ ] Create corpus directories
- [ ] Set up continuous fuzzing (OSS-Fuzz or ClusterFuzz)
- [ ] Create fuzzing dictionary files

##### 1.1.5.2 Parser Fuzzers
- [ ] **fuzz_lexer.c** - Lexer fuzzing
  - [ ] Fuzz with random bytes
  - [ ] Fuzz with grammar-aware mutations
  - [ ] Create seed corpus from test files

- [ ] **fuzz_parser.c** - Parser fuzzing
  - [ ] Fuzz with lexer output
  - [ ] Fuzz with AST mutations
  - [ ] Create seed corpus from examples

- [ ] **fuzz_bytecode.c** - Bytecode fuzzing
  - [ ] Fuzz bytecode loading
  - [ ] Fuzz malformed headers
  - [ ] Fuzz invalid opcodes
  - [ ] Fuzz invalid operands


- [ ] **fuzz_url_parser.c** - URL parser
  - [ ] Fuzz scheme
  - [ ] Fuzz host
  - [ ] Fuzz path
  - [ ] Fuzz query
  - [ ] Create seed corpus

##### 1.1.5.4 Value Fuzzers
- [ ] **fuzz_nanbox.c** - NaN-boxing
  - [ ] Fuzz all 64-bit patterns
  - [ ] Verify type detection
  - [ ] Verify extraction

- [ ] **fuzz_json_parse.c** - JSON parsing (if applicable)
  - [ ] Fuzz JSON input
  - [ ] Verify output correctness

##### 1.1.5.5 Fuzzing CI Integration
- [ ] Run fuzzers for minimum 1 hour in CI
- [ ] Archive crash artifacts
- [ ] Regression test all crashes
- [ ] Track corpus coverage over time

#### 1.1.6 Property-Based Testing

##### 1.1.6.1 Infrastructure
- [ ] Integrate property-based testing framework (theft, quickcheck-like)
- [ ] Create generators for all value types
- [ ] Create generators for bytecode

##### 1.1.6.2 Properties
- [ ] **Array properties**
  - [ ] push then length increases by 1
  - [ ] get(push(arr, x), len-1) == x
  - [ ] concat length == len(a) + len(b)

- [ ] **Map properties**
  - [ ] set then get returns value
  - [ ] delete then get returns nil
  - [ ] size after set increases or stays same

- [ ] **String properties**
  - [ ] concat length == len(a) + len(b)
  - [ ] slice(0, len) == original
  - [ ] intern(s) == intern(s) for same string

- [ ] **Scheduler properties**
  - [ ] all spawned blocks eventually terminate or wait
  - [ ] no block executes more than reduction limit
  - [ ] terminated count <= spawned count

- [ ] **GC properties**
  - [ ] reachable objects never freed
  - [ ] unreachable objects eventually freed
  - [ ] bytes_allocated accurate

#### 1.1.7 Code Coverage

##### 1.1.7.1 Coverage Infrastructure
- [ ] Enable gcov/lcov
  ```cmake
  option(COVERAGE "Enable coverage" OFF)
  if(COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
  endif()
  ```
- [ ] Create coverage report generation script
- [ ] Set up coverage upload to Codecov/Coveralls
- [ ] Add coverage badge to README

##### 1.1.7.2 Coverage Targets
- [ ] Achieve 80% line coverage (minimum)
- [ ] Achieve 90% line coverage (target)
- [ ] Achieve 95% line coverage (stretch)
- [ ] Achieve 80% branch coverage (minimum)
- [ ] Achieve 90% branch coverage (target)
- [ ] Document uncovered code with rationale

#### 1.1.8 Benchmark Suite

##### 1.1.8.1 Microbenchmarks
- [ ] **bench_vm_ops.c** - VM operations
  - [ ] Benchmark arithmetic ops
  - [ ] Benchmark comparison ops
  - [ ] Benchmark function calls
  - [ ] Benchmark closure creation
  - [ ] Benchmark array operations
  - [ ] Benchmark map operations
  - [ ] Benchmark string operations

- [ ] **bench_gc.c** - GC performance
  - [ ] Benchmark allocation rate
  - [ ] Benchmark minor GC pause
  - [ ] Benchmark major GC pause
  - [ ] Benchmark GC throughput
  - [ ] Benchmark write barrier overhead

- [ ] **bench_scheduler.c** - Scheduler
  - [ ] Benchmark block spawn rate
  - [ ] Benchmark context switch time
  - [ ] Benchmark message send rate
  - [ ] Benchmark work stealing overhead

- [ ] **bench_mailbox.c** - Mailbox
  - [ ] Benchmark single-producer throughput
  - [ ] Benchmark multi-producer throughput
  - [ ] Benchmark latency percentiles

- [ ] **bench_io.c** - File I/O
  - [ ] Benchmark file read throughput
  - [ ] Benchmark file write throughput

##### 1.1.8.2 Macrobenchmarks
- [ ] **bench_skynet.c** - Skynet benchmark
  - [ ] 1 million process spawn and message
  - [ ] Measure total time
  - [ ] Measure memory usage

- [ ] **bench_ring.c** - Ring benchmark
  - [ ] N processes passing message
  - [ ] Measure throughput
  - [ ] Measure latency

- [ ] **bench_parallel_fib.c** - Parallel fibonacci
  - [ ] Measure parallelism efficiency
  - [ ] Measure overhead vs sequential

##### 1.1.8.3 Benchmark Infrastructure
- [ ] Create benchmark runner script
- [ ] Create result storage (JSON/CSV)
- [ ] Create visualization (graphs)
- [ ] Set up continuous benchmarking
- [ ] Create regression alerts

---

## Phase 2: Security Hardening

### 2.1 Macro Goals
- Complete security audit
- Penetration testing
- Static analysis integration
- Dependency security scanning
- Security documentation

### 2.2 Detailed Tasks

#### 2.2.1 Static Analysis

##### 2.2.1.1 Compiler Warnings
- [ ] Enable all warnings
  ```cmake
  add_compile_options(-Wall -Wextra -Wpedantic)
  add_compile_options(-Wformat=2 -Wformat-security)
  add_compile_options(-Wstack-protector)
  add_compile_options(-Wstrict-overflow=5)
  add_compile_options(-Wcast-align)
  add_compile_options(-Wpointer-arith)
  add_compile_options(-Wwrite-strings)
  add_compile_options(-Wconversion)
  add_compile_options(-Wsign-conversion)
  add_compile_options(-Wmissing-prototypes)
  add_compile_options(-Wstrict-prototypes)
  add_compile_options(-Wshadow)
  ```
- [ ] Fix all warnings
- [ ] Enable -Werror in CI

##### 2.2.1.2 Static Analysis Tools
- [ ] **Clang Static Analyzer**
  - [ ] Run scan-build on codebase
  - [ ] Fix all findings
  - [ ] Add to CI

- [ ] **Cppcheck**
  - [ ] Run cppcheck with all checks
  - [ ] Fix all findings
  - [ ] Add to CI

- [ ] **Coverity Scan**
  - [ ] Submit to Coverity Scan
  - [ ] Fix all findings
  - [ ] Set up regular scans

- [ ] **CodeQL**
  - [ ] Set up CodeQL analysis
  - [ ] Enable security queries
  - [ ] Enable quality queries
  - [ ] Fix all findings

- [ ] **Semgrep**
  - [ ] Run security rules
  - [ ] Run C rules
  - [ ] Fix all findings

##### 2.2.1.3 Custom Rules
- [ ] Create rule: no unbounded loops
- [ ] Create rule: no unsafe string functions
- [ ] Create rule: all allocations checked
- [ ] Create rule: all inputs validated

#### 2.2.2 Input Validation

##### 2.2.2.1 File Input
- [ ] **Path validation**
  - [ ] Validate scheme whitelist
  - [ ] Validate host characters
  - [ ] Validate port range
  - [ ] Validate path encoding
  - [ ] Validate query encoding

##### 2.2.2.2 Bytecode Validation
- [ ] **Header validation**
  - [ ] Validate magic number
  - [ ] Validate version compatibility
  - [ ] Validate section sizes
  - [ ] Validate checksum

- [ ] **Instruction validation**
  - [ ] Validate opcode range
  - [ ] Validate register indices
  - [ ] Validate constant indices
  - [ ] Validate jump targets
  - [ ] Validate function arities

##### 2.2.2.3 Configuration Validation
- [ ] Validate all numeric configs have bounds
- [ ] Validate all string configs have length limits
- [ ] Validate all file paths are canonicalized

#### 2.2.3 Memory Safety

##### 2.2.3.1 Buffer Overflow Prevention
- [ ] Audit all array accesses for bounds checks
- [ ] Audit all string operations for length checks
- [ ] Audit all memcpy/memmove for size checks
- [ ] Replace sprintf with snprintf everywhere
- [ ] Replace strcpy with strncpy/strlcpy
- [ ] Replace strcat with strncat/strlcat

##### 2.2.3.2 Integer Overflow Prevention
- [ ] Audit all arithmetic for overflow
- [ ] Use safe arithmetic helpers
  ```c
  bool safe_add_size(size_t a, size_t b, size_t *result);
  bool safe_mul_size(size_t a, size_t b, size_t *result);
  ```
- [ ] Check all size calculations before allocation

##### 2.2.3.3 Use-After-Free Prevention
- [ ] Audit all free() calls for double-free
- [ ] Set pointers to NULL after free
- [ ] Audit callback lifetimes
- [ ] Document ownership semantics

##### 2.2.3.4 Memory Leak Prevention
- [ ] Run Valgrind on all tests
- [ ] Fix all definitely lost
- [ ] Fix all indirectly lost
- [ ] Document intentional still-reachable

#### 2.2.4 Cryptographic Security

##### 2.2.4.1 Random Number Generation
- [ ] Audit all random usage
- [ ] Ensure /dev/urandom or equivalent used
- [ ] No rand() for security-sensitive operations
- [ ] Seed properly initialized

##### 2.2.4.2 Key Management
- [ ] No hardcoded secrets
- [ ] Secure key storage
- [ ] Key rotation support

#### 2.2.5 Denial of Service Prevention

##### 2.2.5.1 Resource Limits
- [ ] **CPU limits**
  - [ ] Reduction limits enforced
  - [ ] No infinite loops possible
  - [ ] Timeout on all blocking operations

- [ ] **Memory limits**
  - [ ] Heap size limits enforced
  - [ ] Stack size limits enforced
  - [ ] Mailbox size limits enforced
  - [ ] Total block count limited

- [ ] **File limits**
  - [ ] File size limits
  - [ ] File count limits
  - [ ] Disk space monitoring

##### 2.2.5.2 Algorithmic Complexity
- [ ] No O(n²) or worse in hot paths
- [ ] Hash collision resistance
- [ ] Regex complexity limits (if applicable)

#### 2.2.6 Penetration Testing

##### 2.2.6.1 Test Plan
- [ ] Define scope
- [ ] Define rules of engagement
- [ ] Define success criteria
- [ ] Schedule testing window
- [ ] Prepare test environment

##### 2.2.6.2 Test Scenarios
- [ ] Attempt buffer overflow via network
- [ ] Attempt integer overflow via network
- [ ] Attempt format string attack
- [ ] Attempt path traversal
- [ ] Attempt capability bypass
- [ ] Attempt sandbox escape
- [ ] Attempt DoS via resource exhaustion
- [ ] Attempt DoS via algorithmic complexity

##### 2.2.6.3 Remediation
- [ ] Triage all findings
- [ ] Fix critical findings immediately
- [ ] Fix high findings within 7 days
- [ ] Fix medium findings within 30 days
- [ ] Document low/informational findings

#### 2.2.7 Dependency Security

##### 2.2.7.1 Inventory
- [ ] List all dependencies
- [ ] Document versions
- [ ] Document sources
- [ ] Document licenses

##### 2.2.7.2 Scanning
- [ ] Set up dependency scanning
- [ ] Monitor for CVEs
- [ ] Automated alerts for vulnerabilities
- [ ] Regular update schedule

#### 2.2.8 Security Documentation

##### 2.2.8.1 Security Policy
- [ ] Create SECURITY.md
- [ ] Define supported versions
- [ ] Define reporting process
- [ ] Define response timeline
- [ ] Define disclosure policy

##### 2.2.8.2 Threat Model
- [ ] Identify assets
- [ ] Identify threat actors
- [ ] Identify attack surfaces
- [ ] Identify mitigations
- [ ] Document trust boundaries

##### 2.2.8.3 Security Hardening Guide
- [ ] Document secure deployment
- [ ] Document capability best practices
- [ ] Document sandbox configuration
- [ ] Document network security
- [ ] Document monitoring for attacks

---

## Phase 3: Reliability Engineering

### 3.1 Macro Goals
- Fault injection testing
- Chaos engineering
- Graceful degradation
- Recovery procedures

### 3.2 Detailed Tasks

#### 3.2.1 Fault Injection

##### 3.2.1.1 Infrastructure
- [ ] Create fault injection framework
  ```c
  typedef enum FaultType {
      FAULT_ALLOC_FAIL,
      FAULT_SEND_FAIL,
      FAULT_RECV_FAIL,
      FAULT_TIMEOUT,
      FAULT_CRASH,
  } FaultType;

  void fault_inject_enable(FaultType type, double probability);
  void fault_inject_disable(FaultType type);
  ```

##### 3.2.1.2 Memory Faults
- [ ] Test OOM during allocation
- [ ] Test OOM during GC
- [ ] Test OOM during message copy
- [ ] Test OOM during array growth
- [ ] Test OOM during map growth
- [ ] Verify graceful handling in all cases

##### 3.2.1.3 Process Faults
- [ ] Test random process crashes
- [ ] Test supervisor crash
- [ ] Test worker crash
- [ ] Test scheduler crash
- [ ] Verify recovery in all cases

##### 3.2.1.4 Timing Faults
- [ ] Test slow operations
- [ ] Test operation timeout
- [ ] Test clock skew
- [ ] Verify timeout handling

#### 3.2.2 Chaos Engineering

##### 3.2.2.1 Chaos Tests
- [ ] **Network partition**
  - [ ] Simulate partition between nodes
  - [ ] Verify continued operation
  - [ ] Verify partition healing

- [ ] **Process kill**
  - [ ] Randomly kill processes
  - [ ] Verify supervisor restart
  - [ ] Verify state recovery

- [ ] **Resource exhaustion**
  - [ ] Exhaust memory
  - [ ] Exhaust file descriptors
  - [ ] Exhaust CPU
  - [ ] Verify graceful degradation

- [ ] **Clock manipulation**
  - [ ] Jump clock forward
  - [ ] Jump clock backward
  - [ ] Verify timer correctness

##### 3.2.2.2 Chaos Framework
- [ ] Create chaos test runner
- [ ] Define chaos scenarios
- [ ] Define success criteria
- [ ] Integrate with CI (controlled)

#### 3.2.3 Error Handling

##### 3.2.3.1 Error Audit
- [ ] Audit all function return values checked
- [ ] Audit all error paths tested
- [ ] Audit all error messages meaningful
- [ ] Audit all errors logged

##### 3.2.3.2 Error Propagation
- [ ] Document error propagation patterns
- [ ] Ensure errors not silently swallowed
- [ ] Ensure errors have context
- [ ] Ensure errors actionable

##### 3.2.3.3 Error Recovery
- [ ] Document recovery procedures
- [ ] Implement automatic recovery where possible
- [ ] Test manual recovery procedures

#### 3.2.4 Graceful Degradation

##### 3.2.4.1 Load Shedding
- [ ] Implement request queue limits
- [ ] Implement rejection when overloaded
- [ ] Return meaningful overload errors
- [ ] Document load shedding behavior

##### 3.2.4.2 Circuit Breakers
- [ ] Implement circuit breaker pattern
- [ ] Track failure rates
- [ ] Open circuit on threshold
- [ ] Half-open testing
- [ ] Automatic recovery

##### 3.2.4.3 Backpressure
- [ ] Implement backpressure in mailboxes
- [ ] Document backpressure behavior
- [ ] Test backpressure scenarios

#### 3.2.5 Data Integrity

##### 3.2.5.1 Checksums
- [ ] Bytecode checksum verification
- [ ] Network data checksum (if applicable)
- [ ] File checksum verification

##### 3.2.5.2 Consistency
- [ ] Verify data structures consistent after operations
- [ ] Verify state consistent after crashes
- [ ] Verify state consistent after recovery

#### 3.2.6 Recovery Procedures

##### 3.2.6.1 Process Recovery
- [ ] Document process crash recovery
- [ ] Document supervisor restart behavior
- [ ] Document state loss on crash
- [ ] Test recovery scenarios

##### 3.2.6.2 System Recovery
- [ ] Document system restart procedure
- [ ] Document state persistence (if any)
- [ ] Document data recovery
- [ ] Test full system recovery

---

## Phase 4: Performance Engineering

### 4.1 Macro Goals
- Profile and optimize hot paths
- Memory optimization
- Latency optimization
- Throughput optimization

### 4.2 Detailed Tasks

#### 4.2.1 Profiling

##### 4.2.1.1 CPU Profiling
- [ ] Set up perf profiling
- [ ] Set up flamegraph generation
- [ ] Identify top CPU consumers
- [ ] Profile VM dispatch loop
- [ ] Profile GC
- [ ] Profile scheduler
- [ ] Profile mailbox

##### 4.2.1.2 Memory Profiling
- [ ] Set up massif (Valgrind)
- [ ] Identify memory hotspots
- [ ] Profile allocation patterns
- [ ] Profile GC memory overhead
- [ ] Profile per-block overhead

##### 4.2.1.3 Cache Profiling
- [ ] Set up cachegrind
- [ ] Identify cache misses
- [ ] Profile data structure layout
- [ ] Profile instruction cache

##### 4.2.1.4 Lock Profiling
- [ ] Set up lock contention profiling
- [ ] Identify contended locks
- [ ] Measure lock hold times
- [ ] Measure lock wait times

#### 4.2.2 VM Optimizations

##### 4.2.2.1 Dispatch Optimization
- [ ] Verify computed goto used (GCC/Clang)
- [ ] Measure dispatch overhead
- [ ] Consider direct threading
- [ ] Consider superinstructions

##### 4.2.2.2 Hot Path Optimization
- [ ] Optimize arithmetic ops
  - [ ] Inline fast paths
  - [ ] Avoid function calls
- [ ] Optimize array access
  - [ ] Bounds check elimination
  - [ ] Inline array_get
- [ ] Optimize map access
  - [ ] Inline cache effectiveness
  - [ ] Hash function optimization
- [ ] Optimize function calls
  - [ ] Reduce frame setup overhead
  - [ ] Optimize argument passing

##### 4.2.2.3 NaN-box Optimization
- [ ] Profile nanbox operations
- [ ] Optimize type checks
- [ ] Optimize extraction
- [ ] Consider alternative encodings

##### 4.2.2.4 Register VM Optimization
- [ ] Profile register allocation
- [ ] Optimize register spilling
- [ ] Optimize frame layout

#### 4.2.3 GC Optimizations

##### 4.2.3.1 Allocation Optimization
- [ ] Profile allocation rate
- [ ] Optimize small object allocation
- [ ] Consider bump allocation
- [ ] Consider object pools
- [ ] Reduce allocation in hot paths

##### 4.2.3.2 Collection Optimization
- [ ] Profile GC pause times
- [ ] Optimize mark phase
- [ ] Optimize sweep phase
- [ ] Tune generation thresholds
- [ ] Reduce write barrier overhead

##### 4.2.3.3 Memory Layout
- [ ] Optimize object layout
- [ ] Reduce padding
- [ ] Improve cache locality
- [ ] Consider structure packing

#### 4.2.4 Scheduler Optimizations

##### 4.2.4.1 Context Switch Optimization
- [ ] Profile context switch time
- [ ] Reduce switch overhead
- [ ] Optimize state save/restore

##### 4.2.4.2 Work Stealing Optimization
- [ ] Profile steal rate
- [ ] Optimize victim selection
- [ ] Reduce steal overhead
- [ ] Tune stealing parameters

##### 4.2.4.3 Load Balancing
- [ ] Profile load distribution
- [ ] Optimize work distribution
- [ ] Reduce imbalance

#### 4.2.5 I/O Optimizations

##### 4.2.5.1 Parser Optimization
- [ ] Profile JSON parser
- [ ] Optimize string operations
- [ ] Reduce allocations

##### 4.2.5.2 File I/O Optimization
- [ ] Profile I/O patterns
- [ ] Optimize buffer sizes
- [ ] Consider io_uring (Linux)

#### 4.2.6 Memory Optimizations

##### 4.2.6.1 Reduce Allocations
- [ ] Identify allocation hotspots
- [ ] Use stack allocation where possible
- [ ] Use arena allocation where possible
- [ ] Reuse buffers

##### 4.2.6.2 Reduce Memory Usage
- [ ] Profile memory footprint
- [ ] Optimize data structures
- [ ] Reduce per-block overhead
- [ ] Reduce per-value overhead

##### 4.2.6.3 Cache Optimization
- [ ] Optimize data layout for cache
- [ ] Reduce pointer chasing
- [ ] Prefetch where beneficial

#### 4.2.7 Concurrency Optimizations

##### 4.2.7.1 Lock Optimization
- [ ] Reduce lock contention
- [ ] Use fine-grained locking
- [ ] Use reader-writer locks where applicable
- [ ] Consider lock-free alternatives

##### 4.2.7.2 Atomic Optimization
- [ ] Profile atomic operations
- [ ] Use appropriate memory orders
- [ ] Reduce false sharing

##### 4.2.7.3 Parallelism
- [ ] Profile parallel efficiency
- [ ] Optimize worker count
- [ ] Reduce coordination overhead

---

## Phase 5: Observability

### 5.1 Macro Goals
- Comprehensive logging
- Metrics collection
- Distributed tracing
- Alerting system

### 5.2 Detailed Tasks

#### 5.2.1 Logging

##### 5.2.1.1 Logging Infrastructure
- [ ] Create logging API
  ```c
  typedef enum LogLevel {
      LOG_TRACE,
      LOG_DEBUG,
      LOG_INFO,
      LOG_WARN,
      LOG_ERROR,
      LOG_FATAL,
  } LogLevel;

  void log_init(LogLevel level, const char *output);
  void log_write(LogLevel level, const char *component, const char *fmt, ...);
  ```
- [ ] Implement structured logging (JSON)
- [ ] Implement log rotation
- [ ] Implement async logging
- [ ] Implement log sampling

##### 5.2.1.2 Log Points
- [ ] **Scheduler logs**
  - [ ] Block spawn
  - [ ] Block terminate
  - [ ] Context switch
  - [ ] Worker start/stop

- [ ] **GC logs**
  - [ ] GC start
  - [ ] GC complete
  - [ ] Promotion events
  - [ ] OOM events

- [ ] **Error logs**
  - [ ] All error conditions
  - [ ] Stack traces where applicable
  - [ ] Context information

##### 5.2.1.3 Log Management
- [ ] Define log retention policy
- [ ] Define log storage requirements
- [ ] Set up log aggregation
- [ ] Set up log search

#### 5.2.2 Metrics

##### 5.2.2.1 Metrics Infrastructure
- [ ] Create metrics API
  ```c
  typedef enum MetricType {
      METRIC_COUNTER,
      METRIC_GAUGE,
      METRIC_HISTOGRAM,
  } MetricType;

  void metric_counter_inc(const char *name, uint64_t value);
  void metric_gauge_set(const char *name, double value);
  void metric_histogram_observe(const char *name, double value);
  ```
- [ ] Implement Prometheus exporter
- [ ] Implement StatsD exporter
- [ ] Implement periodic export

##### 5.2.2.2 System Metrics
- [ ] CPU usage
- [ ] Memory usage
- [ ] Open file descriptors
- [ ] Thread count
- [ ] Uptime

##### 5.2.2.3 Runtime Metrics
- [ ] **Scheduler metrics**
  - [ ] blocks_spawned_total
  - [ ] blocks_terminated_total
  - [ ] blocks_active
  - [ ] blocks_waiting
  - [ ] context_switches_total
  - [ ] reductions_total
  - [ ] run_queue_length
  - [ ] worker_count
  - [ ] steals_total
  - [ ] steals_successful_total

- [ ] **GC metrics**
  - [ ] gc_collections_total
  - [ ] gc_minor_collections_total
  - [ ] gc_major_collections_total
  - [ ] gc_pause_seconds (histogram)
  - [ ] heap_bytes_allocated
  - [ ] heap_bytes_limit
  - [ ] young_gen_bytes
  - [ ] old_gen_bytes

- [ ] **Mailbox metrics**
  - [ ] messages_sent_total
  - [ ] messages_received_total
  - [ ] mailbox_depth (histogram)

##### 5.2.2.4 Application Metrics
- [ ] Allow user-defined metrics
- [ ] Metrics cardinality limits
- [ ] Metrics documentation

#### 5.2.3 Tracing

##### 5.2.3.1 Tracing Infrastructure
- [ ] Implement trace context
  ```c
  typedef struct TraceContext {
      uint64_t trace_id;
      uint64_t span_id;
      uint64_t parent_span_id;
  } TraceContext;

  TraceContext *trace_start(const char *operation);
  void trace_end(TraceContext *ctx);
  void trace_add_tag(TraceContext *ctx, const char *key, const char *value);
  ```
- [ ] Implement OpenTelemetry exporter
- [ ] Implement Jaeger exporter
- [ ] Implement Zipkin exporter

##### 5.2.3.2 Trace Points
- [ ] **Block lifecycle**
  - [ ] Spawn (new trace)
  - [ ] Message send (propagate context)
  - [ ] Message receive (continue trace)
  - [ ] Terminate (end trace)

- [ ] **I/O operations**
  - [ ] File read (new span)
  - [ ] File write (new span)

- [ ] **Internal operations**
  - [ ] GC (span)
  - [ ] Compilation (span)

##### 5.2.3.3 Trace Management
- [ ] Sampling configuration
- [ ] Trace storage requirements
- [ ] Trace retention policy

#### 5.2.4 Health Checks

##### 5.2.4.1 Liveness Check
- [ ] Implement liveness endpoint
- [ ] Check scheduler running
- [ ] Check workers alive
- [ ] Quick response (<100ms)

##### 5.2.4.2 Readiness Check
- [ ] Implement readiness endpoint
- [ ] Check system initialized
- [ ] Check dependencies available
- [ ] Check resources available

##### 5.2.4.3 Deep Health Check
- [ ] Implement deep health endpoint
- [ ] Full system verification
- [ ] Dependency health
- [ ] Detailed status

#### 5.2.5 Alerting

##### 5.2.5.1 Alert Rules
- [ ] **Critical alerts**
  - [ ] System down
  - [ ] OOM
  - [ ] Deadlock detected
  - [ ] Security breach

- [ ] **Warning alerts**
  - [ ] High memory usage (>80%)
  - [ ] High CPU usage (>80%)
  - [ ] High error rate (>1%)
  - [ ] High latency (>p99 threshold)

- [ ] **Informational alerts**
  - [ ] Deployment complete
  - [ ] Configuration change
  - [ ] Unusual patterns

##### 5.2.5.2 Alert Infrastructure
- [ ] Define alert routing
- [ ] Define escalation policy
- [ ] Define on-call rotation
- [ ] Set up PagerDuty/OpsGenie integration

#### 5.2.6 Dashboards

##### 5.2.6.1 System Dashboard
- [ ] CPU usage graph
- [ ] Memory usage graph
- [ ] Block count over time
- [ ] Message rate over time
- [ ] Error rate over time

##### 5.2.6.2 Performance Dashboard
- [ ] Request latency percentiles
- [ ] GC pause percentiles
- [ ] Throughput over time
- [ ] Queue depths

##### 5.2.6.3 Capacity Dashboard
- [ ] Resource utilization trends
- [ ] Growth projections
- [ ] Headroom calculations

---

## Phase 6: Documentation

### 6.1 Macro Goals
- Complete API documentation
- Architecture documentation
- Operations documentation
- User documentation

### 6.2 Detailed Tasks

#### 6.2.1 API Documentation

##### 6.2.1.1 Public API
- [ ] Document all public functions
- [ ] Document all public types
- [ ] Document all public macros
- [ ] Include usage examples
- [ ] Include error conditions
- [ ] Include thread safety notes
- [ ] Generate Doxygen/documentation

##### 6.2.1.2 Header Documentation
For each header file:
- [ ] **vm/vm.h** - VM API
- [ ] **vm/gc.h** - GC API
- [ ] **vm/value.h** - Value types API
- [ ] **vm/bytecode.h** - Bytecode API
- [ ] **runtime/scheduler.h** - Scheduler API
- [ ] **runtime/block.h** - Block API
- [ ] **runtime/worker.h** - Worker API
- [ ] **runtime/mailbox.h** - Mailbox API
- [ ] **runtime/timer.h** - Timer API
- [ ] **runtime/capability.h** - Capability API
- [ ] **runtime/supervisor.h** - Supervisor API
- [ ] **types/array.h** - Array API
- [ ] **types/map.h** - Map API
- [ ] **types/string.h** - String API
- [ ] **types/closure.h** - Closure API

#### 6.2.2 Architecture Documentation

##### 6.2.2.1 System Architecture
- [ ] High-level architecture diagram
- [ ] Component interactions
- [ ] Data flow diagrams
- [ ] Deployment architecture

##### 6.2.2.2 Component Documentation
- [ ] **VM architecture**
  - [ ] Instruction set reference
  - [ ] Register allocation
  - [ ] Stack management
  - [ ] Value representation

- [ ] **GC architecture**
  - [ ] Collection algorithms
  - [ ] Generational design
  - [ ] Write barriers
  - [ ] Tuning guide

- [ ] **Scheduler architecture**
  - [ ] Scheduling algorithm
  - [ ] Work stealing design
  - [ ] Reduction counting
  - [ ] Worker management

- [ ] **I/O architecture**
  - [ ] File operations
  - [ ] Sandbox enforcement

##### 6.2.2.3 Design Decisions
- [ ] Expand CLAUDE.md with rationale
- [ ] Document alternatives considered
- [ ] Document trade-offs made
- [ ] Document future considerations

#### 6.2.3 Operations Documentation

##### 6.2.3.1 Deployment Guide
- [ ] Prerequisites
- [ ] Installation steps
- [ ] Configuration options
- [ ] Verification steps
- [ ] Rollback procedure

##### 6.2.3.2 Configuration Guide
- [ ] All configuration options
- [ ] Default values
- [ ] Recommended values
- [ ] Environment-specific tuning

##### 6.2.3.3 Monitoring Guide
- [ ] Key metrics to watch
- [ ] Dashboard setup
- [ ] Alert configuration
- [ ] Log interpretation

##### 6.2.3.4 Troubleshooting Guide
- [ ] Common issues and solutions
- [ ] Diagnostic procedures
- [ ] Log analysis
- [ ] Debug techniques

##### 6.2.3.5 Runbooks
- [ ] **Incident response**
  - [ ] System down
  - [ ] High latency
  - [ ] High error rate
  - [ ] Memory exhaustion
  - [ ] Security incident

- [ ] **Maintenance procedures**
  - [ ] Deployment
  - [ ] Configuration change
  - [ ] Scaling
  - [ ] Backup/restore

#### 6.2.4 User Documentation

##### 6.2.4.1 Language Guide
- [ ] Language syntax
- [ ] Type system
- [ ] Standard library
- [ ] Best practices

##### 6.2.4.2 Tutorial
- [ ] Getting started
- [ ] First program
- [ ] Concurrency tutorial
- [ ] Network programming
- [ ] Error handling

##### 6.2.4.3 Examples
- [ ] Hello world
- [ ] Echo server
- [ ] Worker pool
- [ ] Pub/sub system
- [ ] State machine
- [ ] File processor

##### 6.2.4.4 FAQ
- [ ] Common questions
- [ ] Common errors
- [ ] Performance tips
- [ ] Security tips

---

## Phase 7: Operations

### 7.1 Macro Goals
- Automated deployment
- Configuration management
- Backup and recovery
- Incident management

### 7.2 Detailed Tasks

#### 7.2.1 Build and Release

##### 7.2.1.1 Build System
- [ ] Reproducible builds
- [ ] Build versioning
- [ ] Build artifacts
- [ ] Build signing

##### 7.2.1.2 Release Process
- [ ] Version numbering (SemVer)
- [ ] Changelog generation
- [ ] Release notes template
- [ ] Release checklist
- [ ] Release automation

##### 7.2.1.3 Artifact Management
- [ ] Binary artifacts
- [ ] Source archives
- [ ] Docker images
- [ ] Package repositories

#### 7.2.2 CI/CD

##### 7.2.2.1 Continuous Integration
- [ ] Build on every commit
- [ ] Run all tests
- [ ] Run all linters
- [ ] Run all sanitizers
- [ ] Generate coverage reports
- [ ] Generate documentation

##### 7.2.2.2 Continuous Delivery
- [ ] Automated staging deployment
- [ ] Automated integration tests
- [ ] Manual production approval
- [ ] Automated production deployment
- [ ] Automated rollback

##### 7.2.2.3 Pipeline Configuration
- [ ] GitHub Actions workflow
- [ ] GitLab CI configuration
- [ ] Jenkins pipeline
- [ ] Build matrix (OS, compiler)

#### 7.2.3 Deployment

##### 7.2.3.1 Deployment Strategies
- [ ] Rolling deployment
- [ ] Blue-green deployment
- [ ] Canary deployment
- [ ] Feature flags

##### 7.2.3.2 Deployment Automation
- [ ] Ansible playbooks
- [ ] Terraform modules
- [ ] Kubernetes manifests
- [ ] Docker Compose

##### 7.2.3.3 Deployment Verification
- [ ] Health check verification
- [ ] Smoke tests
- [ ] Integration tests
- [ ] Performance verification

#### 7.2.4 Configuration Management

##### 7.2.4.1 Configuration Storage
- [ ] Configuration file format
- [ ] Environment variables
- [ ] Secrets management
- [ ] Configuration versioning

##### 7.2.4.2 Configuration Validation
- [ ] Schema validation
- [ ] Semantic validation
- [ ] Pre-deployment validation

##### 7.2.4.3 Configuration Updates
- [ ] Hot reload support
- [ ] Graceful restart for changes
- [ ] Configuration rollback

#### 7.2.5 Backup and Recovery

##### 7.2.5.1 Backup Strategy
- [ ] What to backup
- [ ] Backup frequency
- [ ] Backup retention
- [ ] Backup verification

##### 7.2.5.2 Recovery Procedures
- [ ] Recovery time objective (RTO)
- [ ] Recovery point objective (RPO)
- [ ] Recovery steps
- [ ] Recovery verification

##### 7.2.5.3 Disaster Recovery
- [ ] DR site setup
- [ ] Failover procedure
- [ ] Failback procedure
- [ ] DR testing schedule

#### 7.2.6 Incident Management

##### 7.2.6.1 Incident Process
- [ ] Incident declaration
- [ ] Incident classification
- [ ] Incident communication
- [ ] Incident resolution
- [ ] Post-incident review

##### 7.2.6.2 On-Call
- [ ] On-call rotation
- [ ] Escalation procedures
- [ ] On-call handoff
- [ ] On-call compensation

##### 7.2.6.3 Postmortems
- [ ] Postmortem template
- [ ] Blameless culture
- [ ] Action item tracking
- [ ] Postmortem sharing

---

## Phase 8: Compliance

### 8.1 Macro Goals
- Security compliance
- Code quality standards
- Licensing compliance

### 8.2 Detailed Tasks

#### 8.2.1 Security Compliance

##### 8.2.1.1 Standards
- [ ] OWASP compliance review
- [ ] CWE coverage analysis
- [ ] SANS Top 25 review
- [ ] CVE monitoring

##### 8.2.1.2 Audits
- [ ] Internal security audit
- [ ] External security audit
- [ ] Penetration test
- [ ] Audit remediation

##### 8.2.1.3 Certifications (if applicable)
- [ ] SOC 2 preparation
- [ ] ISO 27001 preparation
- [ ] FedRAMP preparation

#### 8.2.2 Code Quality

##### 8.2.2.1 Coding Standards
- [ ] Define C coding standard
- [ ] Enforce with clang-format
- [ ] Code review checklist
- [ ] Automated enforcement

##### 8.2.2.2 Quality Metrics
- [ ] Cyclomatic complexity limits
- [ ] Function length limits
- [ ] File length limits
- [ ] Duplication limits

##### 8.2.2.3 Technical Debt
- [ ] Debt inventory
- [ ] Debt prioritization
- [ ] Debt reduction plan
- [ ] Debt tracking

#### 8.2.3 Licensing

##### 8.2.3.1 License Compliance
- [ ] Verify MIT license compatibility
- [ ] Dependency license audit
- [ ] License documentation
- [ ] License notices in files

##### 8.2.3.2 Third-Party Components
- [ ] Component inventory
- [ ] License verification
- [ ] Attribution requirements
- [ ] Update procedures

---

## Phase 9: Feature Completion

### 9.1 Macro Goals
- Complete distribution layer
- Complete hot code reloading
- Complete all WIP features

### 9.2 Detailed Tasks

#### 9.2.1 Distribution (src/dist/)

##### 9.2.1.1 Node Discovery
- [ ] Implement node registration
- [ ] Implement node discovery
- [ ] Implement health checking
- [ ] Implement node removal

##### 9.2.1.2 Remote Messaging
- [ ] Implement remote send
- [ ] Implement remote receive
- [ ] Implement message serialization
- [ ] Implement message routing

##### 9.2.1.3 Remote Spawning
- [ ] Implement remote spawn
- [ ] Implement remote monitoring
- [ ] Implement remote linking

##### 9.2.1.4 Cluster Management
- [ ] Implement cluster join
- [ ] Implement cluster leave
- [ ] Implement partition handling
- [ ] Implement cluster state

##### 9.2.1.5 Distribution Testing
- [ ] Multi-node tests
- [ ] Network partition tests
- [ ] Node failure tests
- [ ] Performance tests

#### 9.2.2 Hot Code Reloading

##### 9.2.2.1 Code Versioning
- [ ] Module versioning
- [ ] Version compatibility
- [ ] Version tracking

##### 9.2.2.2 Reload Mechanism
- [ ] Suspend block
- [ ] Load new code
- [ ] Migrate state
- [ ] Resume block

##### 9.2.2.3 Reload Safety
- [ ] Rollback on failure
- [ ] State migration validation
- [ ] Compatibility checking

##### 9.2.2.4 Reload Testing
- [ ] Unit tests
- [ ] Integration tests
- [ ] Stress tests

#### 9.2.3 Missing Features

##### 9.2.3.1 Language Features
- [ ] Pattern matching completeness
- [ ] Generic types completeness
- [ ] Macro system (if planned)
- [ ] REPL improvements

##### 9.2.3.2 Runtime Features
- [ ] Process dictionary
- [ ] Process groups completeness
- [ ] Named processes
- [ ] Ports/NIFs (if planned)

##### 9.2.3.3 Standard Library
- [ ] File I/O module
- [ ] JSON module
- [ ] Crypto module
- [ ] Date/Time module

#### 9.2.4 Networking Module (Hybrid Architecture)

**Design Decision:** Implement networking with a hybrid approach - low-level primitives in C, protocol handling in Agim (.im). This mirrors Erlang/OTP where BEAM provides TCP primitives but HTTP servers (cowboy, hackney) are written in Erlang.

##### 9.2.4.1 C Layer - TCP Primitives
Low-level socket operations remain in C for performance and direct syscall access.

- [ ] **tcp.h / tcp.c** - TCP socket primitives
  - [ ] `tcp_connect(host, port, timeout)` - Establish connection
  - [ ] `tcp_listen(port, backlog)` - Create listener
  - [ ] `tcp_accept(listener)` - Accept connection
  - [ ] `tcp_read(socket, max_bytes)` - Read bytes
  - [ ] `tcp_write(socket, bytes, len)` - Write bytes
  - [ ] `tcp_close(socket)` - Close connection
  - [ ] `tcp_set_timeout(socket, ms)` - Set read/write timeout
  - [ ] `tcp_get_peer(socket)` - Get peer address

- [ ] **Expose to VM** - Bytecode opcodes
  - [ ] `OP_TCP_CONNECT` - Connect opcode
  - [ ] `OP_TCP_LISTEN` - Listen opcode
  - [ ] `OP_TCP_ACCEPT` - Accept opcode
  - [ ] `OP_TCP_READ` - Read opcode
  - [ ] `OP_TCP_WRITE` - Write opcode
  - [ ] `OP_TCP_CLOSE` - Close opcode

- [ ] **Capability integration**
  - [ ] `CAP_NET_CONNECT` - Allow outbound connections
  - [ ] `CAP_NET_LISTEN` - Allow inbound connections
  - [ ] Sandbox rules for allowed hosts/ports

##### 9.2.4.2 C Layer - TLS (Optional)
Crypto operations stay in C for security (audited libraries).

- [ ] **tls.h / tls.c** - TLS wrapper
  - [ ] `tls_wrap(socket, config)` - Upgrade to TLS
  - [ ] `tls_read(socket, max_bytes)` - Secure read
  - [ ] `tls_write(socket, bytes, len)` - Secure write
  - [ ] Certificate validation
  - [ ] Cipher suite configuration

- [ ] **TLS library integration**
  - [ ] BearSSL (minimal footprint) or
  - [ ] OpenSSL (widely available) or
  - [ ] LibreSSL (security focused)

##### 9.2.4.3 Agim Layer - HTTP Client (lib/std/http.im)
HTTP protocol parsing in Agim for safety and hot-reloadability.

- [ ] **URL parsing**
  ```tofu
  struct Url {
      scheme: string,
      host: string,
      port: int,
      path: string,
      query: Option<string>
  }
  fn parse_url(url: string) -> Result<Url, string>
  ```

- [ ] **Request/Response types**
  ```tofu
  struct Request {
      method: string,
      url: Url,
      headers: map<string, string>,
      body: Option<[byte]>
  }

  struct Response {
      status: int,
      headers: map<string, string>,
      body: [byte]
  }
  ```

- [ ] **HTTP client functions**
  - [ ] `http.get(url)` - GET request
  - [ ] `http.post(url, body)` - POST request
  - [ ] `http.request(request)` - Custom request
  - [ ] Header parsing (chunked, content-length)
  - [ ] Redirect following (configurable)
  - [ ] Timeout handling

- [ ] **Connection pooling** (actor-based)
  - [ ] Pool manager block
  - [ ] Keep-alive support
  - [ ] Max connections per host

##### 9.2.4.4 Agim Layer - HTTP Server (lib/std/http_server.im)
Each connection handled by a block for natural concurrency.

- [ ] **Server lifecycle**
  ```tofu
  fn http.listen(port: int, handler: fn(Request) -> Response)
  fn http.stop(server: Pid)
  ```

- [ ] **Request handling**
  - [ ] Spawn block per connection
  - [ ] Parse HTTP/1.1 requests
  - [ ] Route to handler
  - [ ] Send response
  - [ ] Keep-alive support

- [ ] **Middleware support**
  - [ ] Logging middleware
  - [ ] Auth middleware
  - [ ] Rate limiting middleware

##### 9.2.4.5 Agim Layer - WebSocket (lib/std/websocket.im)

- [ ] **WebSocket client**
  ```tofu
  fn ws.connect(url: string) -> Result<WebSocket, string>
  fn ws.send(ws: WebSocket, message: string)
  fn ws.receive(ws: WebSocket) -> Result<string, string>
  fn ws.close(ws: WebSocket)
  ```

- [ ] **WebSocket server**
  - [ ] Upgrade from HTTP
  - [ ] Frame parsing/encoding
  - [ ] Ping/pong handling
  - [ ] Per-connection blocks

##### 9.2.4.6 Benefits of Hybrid Approach

| Aspect | C Layer | Agim Layer |
|--------|---------|------------|
| **What** | TCP sockets, TLS | HTTP, WebSocket protocols |
| **Why** | Performance, syscalls | Safety, maintainability |
| **Memory** | Manual (careful) | GC (safe from overflows) |
| **Updates** | Recompile | Hot reload |
| **Security** | Audited crypto libs | No buffer overflow in parsers |
| **Concurrency** | OS threads | Actor model (blocks) |

##### 9.2.4.7 Testing Strategy

- [ ] **C layer tests**
  - [ ] Socket lifecycle tests
  - [ ] Timeout tests
  - [ ] Error handling tests
  - [ ] Fuzzing TCP input

- [ ] **Agim layer tests**
  - [ ] HTTP parsing tests
  - [ ] Protocol conformance tests
  - [ ] Integration tests
  - [ ] Load tests (many connections)

- [ ] **End-to-end tests**
  - [ ] HTTP client against real servers
  - [ ] HTTP server with test clients
  - [ ] WebSocket echo server
  - [ ] TLS verification

---

## Phase 10: Production Burn-in

### 10.1 Macro Goals
- Staged production rollout
- Real-world validation
- Performance baseline
- Operational readiness

### 10.2 Detailed Tasks

#### 10.2.1 Staging Environment

##### 10.2.1.1 Setup
- [ ] Production-like hardware
- [ ] Production-like configuration
- [ ] Production-like data
- [ ] Monitoring setup

##### 10.2.1.2 Testing
- [ ] Functional testing
- [ ] Performance testing
- [ ] Failure testing
- [ ] Security testing

#### 10.2.2 Canary Deployment

##### 10.2.2.1 Canary Setup
- [ ] 1% traffic routing
- [ ] Separate metrics
- [ ] Comparison dashboards
- [ ] Auto-rollback rules

##### 10.2.2.2 Canary Validation
- [ ] Error rate comparison
- [ ] Latency comparison
- [ ] Resource usage comparison
- [ ] Functional validation

#### 10.2.3 Gradual Rollout

##### 10.2.3.1 Rollout Stages
- [ ] 1% traffic
- [ ] 5% traffic
- [ ] 25% traffic
- [ ] 50% traffic
- [ ] 100% traffic

##### 10.2.3.2 Validation at Each Stage
- [ ] Metric validation
- [ ] User feedback
- [ ] Error investigation
- [ ] Performance validation

#### 10.2.4 Production Baseline

##### 10.2.4.1 Performance Baseline
- [ ] Establish latency baseline
- [ ] Establish throughput baseline
- [ ] Establish resource baseline
- [ ] Document baseline

##### 10.2.4.2 Operational Baseline
- [ ] On-call incident rate
- [ ] Manual intervention rate
- [ ] Deployment success rate
- [ ] MTTR baseline

#### 10.2.5 Production Readiness Review

##### 10.2.5.1 Checklist
- [ ] All critical tests passing
- [ ] All security findings resolved
- [ ] All documentation complete
- [ ] All runbooks tested
- [ ] All alerts configured
- [ ] All dashboards working
- [ ] All metrics collecting
- [ ] All logs flowing
- [ ] Disaster recovery tested
- [ ] On-call trained

##### 10.2.5.2 Sign-off
- [ ] Engineering sign-off
- [ ] Security sign-off
- [ ] Operations sign-off
- [ ] Management sign-off

---

## Detailed Task Breakdown

### Task ID Format
`P{phase}.{section}.{subsection}.{task}`

Example: `P1.1.1.1` = Phase 1, Section 1, Subsection 1, Task 1

### Priority Levels
- **P0**: Blocker - Must be done before any deployment
- **P1**: Critical - Must be done before production
- **P2**: High - Should be done before production
- **P3**: Medium - Can be done after initial production
- **P4**: Low - Nice to have

### Task Status
- [ ] Not Started
- [~] In Progress
- [x] Complete
- [-] Blocked
- [!] Needs Review

### Master Task List

| ID | Task | Priority | Status | Dependencies | Estimate |
|----|------|----------|--------|--------------|----------|
| P1.1.1.1 | test_vm_stack_operations.c | P1 | [x] | None | 2d |
| P1.1.1.2 | test_vm_arithmetic.c | P1 | [ ] | None | 2d |
| P1.1.1.3 | test_vm_comparison.c | P1 | [ ] | None | 1d |
| P1.1.1.4 | test_vm_control_flow.c | P1 | [ ] | None | 2d |
| P1.1.1.5 | test_vm_functions.c | P1 | [ ] | None | 3d |
| P1.1.1.6 | test_vm_memory.c | P1 | [ ] | None | 2d |
| P1.1.1.7 | test_vm_process.c | P1 | [ ] | None | 2d |
| P1.1.2.1 | test_gc_allocation.c | P1 | [ ] | None | 2d |
| P1.1.2.2 | test_gc_marking.c | P1 | [ ] | None | 2d |
| P1.1.2.3 | test_gc_sweeping.c | P1 | [ ] | None | 2d |
| P1.1.2.4 | test_gc_generational.c | P1 | [ ] | None | 2d |
| P1.1.2.5 | test_gc_concurrent.c | P0 | [ ] | P1.4.2 | 3d |
| P1.1.3.1 | test_scheduler_lifecycle.c | P1 | [ ] | None | 1d |
| P1.1.3.2 | test_scheduler_spawn.c | P1 | [ ] | None | 2d |
| P1.1.3.3 | test_scheduler_registry.c | P1 | [ ] | None | 2d |
| P1.1.3.4 | test_scheduler_runqueue.c | P1 | [ ] | None | 1d |
| P1.1.3.5 | test_scheduler_execution.c | P1 | [ ] | None | 2d |
| P1.1.3.6 | test_scheduler_exit.c | P1 | [ ] | None | 2d |
| P1.1.4.1 | test_block_lifecycle.c | P1 | [x] | None | 1d |
| P1.1.4.2 | test_block_capabilities.c | P1 | [ ] | None | 1d |
| P1.1.4.3 | test_block_linking.c | P1 | [ ] | None | 2d |
| P1.1.4.4 | test_block_monitoring.c | P1 | [ ] | None | 2d |
| P1.1.4.5 | test_block_messaging.c | P1 | [x] | None | 2d |
| P1.1.5.1 | test_worker_deque.c | P1 | [ ] | None | 2d |
| P1.1.5.2 | test_worker_lifecycle.c | P1 | [ ] | None | 1d |
| P1.1.5.3 | test_worker_stealing.c | P1 | [ ] | None | 2d |
| P1.1.5.4 | test_worker_execution.c | P1 | [ ] | None | 2d |
| P1.1.6.1 | test_mailbox_mpsc.c | P1 | [x] | None | 2d |
| P1.1.6.2 | test_mailbox_concurrent.c | P0 | [ ] | P1.4.2 | 2d |
| P1.1.7.1 | test_timer_wheel.c | P2 | [ ] | None | 2d |
| P1.1.8.1 | test_value_creation.c | P1 | [x] | None | 1d |
| P1.1.8.2 | test_value_refcount.c | P1 | [ ] | None | 2d |
| P1.1.8.3 | test_value_cow.c | P1 | [x] | None | 2d |
| P1.1.8.4 | test_value_nanbox.c | P1 | [ ] | None | 1d |
| P1.1.9.1 | test_array_operations.c | P2 | [ ] | None | 1d |
| P1.1.10.1 | test_map_operations.c | P2 | [ ] | None | 1d |
| P1.1.11.1 | test_string_operations.c | P2 | [ ] | None | 1d |
| P1.1.11.2 | test_string_interning.c | P2 | [ ] | None | 1d |
| P1.1.12.1 | test_closure_operations.c | P2 | [ ] | None | 1d |
| P1.1.13.1 | test_file_operations.c | P2 | [x] | None | 2d |
| P1.1.14.1 | test_lexer_tokens.c | P2 | [x] | None | 2d |
| P1.1.14.2 | test_parser_ast.c | P2 | [x] | None | 3d |
| P1.1.14.3 | test_compiler_codegen.c | P2 | [x] | None | 3d |
| P1.1.14.4 | test_typechecker.c | P2 | [ ] | None | 2d |
| P1.1.15.1 | test_supervisor_strategies.c | P2 | [x] | None | 2d |
| P1.1.16.1 | test_sandbox_paths.c | P1 | [x] | None | 2d |
| P1.1.17.1 | test_capabilities.c | P1 | [ ] | None | 2d |
| P1.2.1 | test_integration_messaging.c | P1 | [x] | P1.1.* | 2d |
| P1.2.2 | test_integration_linking.c | P1 | [ ] | P1.1.* | 2d |
| P1.2.3 | test_integration_monitoring.c | P1 | [ ] | P1.1.* | 2d |
| P1.2.4 | test_integration_supervision.c | P1 | [x] | P1.1.* | 2d |
| P1.2.5 | test_integration_scheduling.c | P1 | [x] | P1.1.* | 2d |
| P1.2.6 | test_integration_workers.c | P1 | [x] | P1.1.* | 2d |
| P1.3.1 | test_e2e_echo_server.c | P2 | [ ] | P1.2.* | 2d |
| P1.3.2 | test_e2e_worker_pool.c | P2 | [ ] | P1.2.* | 2d |
| P1.3.3 | test_e2e_pubsub.c | P2 | [ ] | P1.2.* | 2d |
| P1.3.4 | test_e2e_state_machine.c | P3 | [ ] | P1.2.* | 2d |
| P1.3.5 | test_e2e_pipeline.c | P3 | [ ] | P1.2.* | 2d |
| P1.4.1 | ASan setup | P0 | [ ] | None | 1d |
| P1.4.2 | TSan setup | P0 | [ ] | None | 1d |
| P1.4.3 | MSan setup | P1 | [ ] | None | 1d |
| P1.4.4 | UBSan setup | P1 | [ ] | None | 1d |
| P1.5.1 | Fuzzing infrastructure | P0 | [ ] | None | 3d |
| P1.5.2 | fuzz_lexer.c | P1 | [ ] | P1.5.1 | 2d |
| P1.5.3 | fuzz_parser.c | P1 | [ ] | P1.5.1 | 2d |
| P1.5.4 | fuzz_bytecode.c | P0 | [ ] | P1.5.1 | 2d |
| P1.5.5 | fuzz_url_parser.c | P1 | [ ] | P1.5.1 | 1d |
| P1.5.6 | fuzz_nanbox.c | P2 | [ ] | P1.5.1 | 1d |
| P1.6.1 | Property testing setup | P2 | [ ] | None | 2d |
| P1.6.2 | Array properties | P2 | [ ] | P1.6.1 | 1d |
| P1.6.3 | Map properties | P2 | [ ] | P1.6.1 | 1d |
| P1.6.4 | Scheduler properties | P2 | [ ] | P1.6.1 | 2d |
| P1.6.5 | GC properties | P2 | [ ] | P1.6.1 | 2d |
| P1.7.1 | Coverage setup | P1 | [ ] | None | 1d |
| P1.7.2 | Achieve 80% coverage | P1 | [ ] | P1.1.* | 5d |
| P1.7.3 | Achieve 90% coverage | P2 | [ ] | P1.7.2 | 5d |
| P1.8.1 | Microbenchmark suite | P2 | [x] | None | 3d |
| P1.8.2 | Macrobenchmark suite | P2 | [ ] | None | 3d |
| P1.8.3 | Continuous benchmarking | P3 | [ ] | P1.8.* | 2d |
| P2.1.1 | Enable all warnings | P1 | [ ] | None | 1d |
| P2.1.2 | Clang Static Analyzer | P1 | [ ] | None | 2d |
| P2.1.3 | Cppcheck | P1 | [ ] | None | 1d |
| P2.1.4 | Coverity Scan | P2 | [ ] | None | 2d |
| P2.1.5 | CodeQL | P2 | [ ] | None | 2d |
| P2.2.1 | URL validation | P1 | [ ] | None | 1d |
| P2.2.2 | Bytecode validation | P0 | [ ] | None | 3d |
| P2.3.1 | Buffer overflow audit | P0 | [ ] | None | 3d |
| P2.3.2 | Integer overflow audit | P0 | [ ] | None | 2d |
| P2.3.3 | Use-after-free audit | P0 | [ ] | None | 2d |
| P2.3.4 | Memory leak audit | P1 | [ ] | None | 2d |
| P2.4.1 | RNG audit | P1 | [ ] | None | 1d |
| P2.5.1 | CPU limits | P1 | [x] | None | 1d |
| P2.5.2 | Memory limits | P1 | [x] | None | 1d |
| P2.6.1 | Penetration test plan | P1 | [ ] | P2.* | 2d |
| P2.6.2 | Execute pen test | P1 | [ ] | P2.6.1 | 5d |
| P2.6.3 | Remediate findings | P0 | [ ] | P2.6.2 | 5d |
| P2.7.1 | Dependency inventory | P1 | [ ] | None | 1d |
| P2.7.2 | Dependency scanning | P1 | [ ] | P2.7.1 | 1d |
| P2.8.1 | SECURITY.md | P1 | [ ] | None | 1d |
| P2.8.2 | Threat model | P1 | [ ] | None | 3d |
| P2.8.3 | Security hardening guide | P2 | [ ] | None | 2d |
| P3.1.1 | Fault injection framework | P2 | [ ] | None | 3d |
| P3.1.2 | Memory fault tests | P2 | [ ] | P3.1.1 | 2d |
| P3.1.3 | Network fault tests | P2 | [ ] | P3.1.1 | 2d |
| P3.1.4 | Process fault tests | P2 | [ ] | P3.1.1 | 2d |
| P3.2.1 | Chaos test framework | P3 | [ ] | P3.1.1 | 3d |
| P3.2.2 | Chaos scenarios | P3 | [ ] | P3.2.1 | 3d |
| P3.3.1 | Error handling audit | P1 | [ ] | None | 3d |
| P3.4.1 | Load shedding | P2 | [ ] | None | 2d |
| P3.4.2 | Circuit breakers | P2 | [ ] | None | 2d |
| P3.4.3 | Backpressure | P2 | [ ] | None | 2d |
| P3.5.1 | Recovery documentation | P2 | [ ] | None | 2d |
| P4.1.1 | CPU profiling setup | P2 | [ ] | None | 1d |
| P4.1.2 | Memory profiling setup | P2 | [ ] | None | 1d |
| P4.2.1 | VM dispatch optimization | P2 | [ ] | P4.1.1 | 3d |
| P4.2.2 | Hot path optimization | P2 | [ ] | P4.1.1 | 5d |
| P4.3.1 | GC optimization | P2 | [ ] | P4.1.* | 5d |
| P4.4.1 | Scheduler optimization | P2 | [ ] | P4.1.* | 3d |
| P4.5.1 | I/O optimization | P3 | [ ] | P4.1.* | 3d |
| P5.1.1 | Logging infrastructure | P1 | [ ] | None | 2d |
| P5.1.2 | Log points | P1 | [ ] | P5.1.1 | 2d |
| P5.2.1 | Metrics infrastructure | P1 | [ ] | None | 3d |
| P5.2.2 | System metrics | P1 | [ ] | P5.2.1 | 1d |
| P5.2.3 | Runtime metrics | P1 | [ ] | P5.2.1 | 2d |
| P5.3.1 | Tracing infrastructure | P2 | [ ] | None | 3d |
| P5.3.2 | Trace points | P2 | [ ] | P5.3.1 | 2d |
| P5.4.1 | Health checks | P1 | [ ] | None | 1d |
| P5.5.1 | Alert rules | P1 | [ ] | P5.2.* | 2d |
| P5.6.1 | Dashboards | P2 | [ ] | P5.2.* | 2d |
| P6.1.1 | API documentation | P1 | [ ] | None | 10d |
| P6.2.1 | Architecture documentation | P1 | [x] | None | 5d |
| P6.3.1 | Operations documentation | P1 | [ ] | P5.* | 5d |
| P6.4.1 | User documentation | P2 | [ ] | None | 10d |
| P7.1.1 | Release process | P1 | [ ] | None | 2d |
| P7.2.1 | CI/CD setup | P1 | [ ] | None | 3d |
| P7.3.1 | Deployment automation | P1 | [ ] | P7.2.1 | 3d |
| P7.4.1 | Configuration management | P1 | [ ] | None | 2d |
| P7.5.1 | Backup procedures | P2 | [ ] | None | 2d |
| P7.6.1 | Incident process | P1 | [ ] | P5.* | 2d |
| P7.6.2 | Runbooks | P1 | [ ] | P6.3.1 | 3d |
| P8.1.1 | Security audit | P1 | [ ] | P2.* | 5d |
| P8.2.1 | Coding standards | P2 | [ ] | None | 2d |
| P8.3.1 | License audit | P1 | [ ] | None | 1d |
| P9.1.1 | Distribution implementation | P3 | [ ] | None | 20d |
| P9.2.1 | Hot reload implementation | P3 | [ ] | None | 15d |
| P9.4.1 | TCP primitives (C layer) | P2 | [ ] | None | 5d |
| P9.4.2 | TCP VM opcodes | P2 | [ ] | P9.4.1 | 3d |
| P9.4.3 | TCP capability integration | P2 | [ ] | P9.4.2 | 2d |
| P9.4.4 | TLS wrapper (C layer) | P3 | [ ] | P9.4.1 | 5d |
| P9.4.5 | HTTP client (lib/std/http.im) | P2 | [ ] | P9.4.2 | 5d |
| P9.4.6 | HTTP server (lib/std/http_server.im) | P3 | [ ] | P9.4.5 | 5d |
| P9.4.7 | WebSocket (lib/std/websocket.im) | P3 | [ ] | P9.4.5 | 4d |
| P9.4.8 | Networking tests | P2 | [ ] | P9.4.5 | 3d |
| P10.1.1 | Staging environment | P1 | [ ] | P7.* | 3d |
| P10.2.1 | Canary deployment | P1 | [ ] | P10.1.1 | 2d |
| P10.3.1 | Production rollout | P1 | [ ] | P10.2.1 | 5d |
| P10.4.1 | Production readiness review | P0 | [ ] | P10.* | 2d |

---

## Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|------------|--------|------------|
| R1 | Undiscovered memory safety bugs | Medium | Critical | Fuzzing, sanitizers, audit |
| R2 | Race conditions in production | Medium | High | TSan, stress testing |
| R3 | Performance regression | Low | Medium | Continuous benchmarking |
| R4 | Security vulnerability | Medium | Critical | Pen testing, audit |
| R5 | OOM under load | Medium | High | Load testing, limits |
| R6 | GC bugs causing corruption | Low | Critical | GC property tests |
| R7 | Scheduler deadlock | Low | Critical | TSan, chaos testing |
| R8 | Data loss on crash | Medium | High | Recovery testing |
| R9 | Dependency vulnerability | Medium | Medium | Dependency scanning |

---

## Success Criteria

### Minimum Viable Production (MVP)
- [ ] All P0 tasks complete
- [ ] All P1 tasks complete
- [ ] 80% test coverage
- [ ] Zero critical/high security findings
- [ ] All sanitizers pass
- [ ] 1 week staging burn-in
- [ ] All runbooks tested

### Full Production Ready
- [ ] All P0-P2 tasks complete
- [ ] 90% test coverage
- [ ] Zero open security findings
- [ ] 1 month staging burn-in
- [ ] Load tested to 2x expected capacity
- [ ] Disaster recovery tested
- [ ] External security audit passed

### Mission Critical
- [ ] All P0-P3 tasks complete
- [ ] 95% test coverage
- [ ] Continuous fuzzing running
- [ ] 3 months production burn-in
- [ ] Load tested to 10x expected capacity
- [ ] Annual security audits
- [ ] SOC 2 / ISO 27001 (if applicable)
- [ ] 99.99% uptime demonstrated

---

## Appendix A: Test File Template

```c
/*
 * Test: {test_name}
 * Component: {component}
 * Priority: P{priority}
 */

#include "test_framework.h"
#include "{header}.h"

/* Setup/Teardown */

static void setup(void) {
    /* Initialize test fixtures */
}

static void teardown(void) {
    /* Cleanup test fixtures */
}

/* Test Cases */

TEST(test_case_1) {
    /* Arrange */

    /* Act */

    /* Assert */
    ASSERT_TRUE(condition);
}

TEST(test_case_2) {
    /* ... */
}

/* Test Suite */

TEST_SUITE({test_name}) {
    TEST_SETUP(setup);
    TEST_TEARDOWN(teardown);

    RUN_TEST(test_case_1);
    RUN_TEST(test_case_2);
}
```

---

## Appendix B: Security Checklist

### Input Validation
- [ ] All external input validated
- [ ] Input length checked
- [ ] Input range checked
- [ ] Input encoding validated
- [ ] No SQL injection (if applicable)
- [ ] No command injection
- [ ] No path traversal

### Memory Safety
- [ ] No buffer overflows
- [ ] No integer overflows
- [ ] No use-after-free
- [ ] No double-free
- [ ] No memory leaks
- [ ] No null pointer dereference

### Cryptography
- [ ] Strong random numbers
- [ ] No hardcoded secrets

### Access Control
- [ ] Capability checks on all operations
- [ ] Sandbox enforced
- [ ] No privilege escalation
- [ ] Default deny

---

## Appendix C: Performance Checklist

### Latency
- [ ] p50 < target
- [ ] p99 < target
- [ ] p99.9 < target
- [ ] No latency spikes

### Throughput
- [ ] Requests/second meets target
- [ ] Messages/second meets target
- [ ] Linear scaling with cores

### Resources
- [ ] Memory usage stable
- [ ] No memory leaks
- [ ] CPU usage appropriate
- [ ] File descriptors bounded

---

## Appendix D: Operational Checklist

### Deployment
- [ ] Automated deployment
- [ ] Rollback procedure tested
- [ ] Health checks configured
- [ ] Deployment verification

### Monitoring
- [ ] All metrics collecting
- [ ] All logs flowing
- [ ] Dashboards working
- [ ] Alerts configured

### Documentation
- [ ] Runbooks available
- [ ] Architecture documented
- [ ] Configuration documented
- [ ] Troubleshooting guide

### On-Call
- [ ] Rotation scheduled
- [ ] Escalation defined
- [ ] Contact information current
- [ ] On-call trained

---

*Document generated: 2026-02-01*
*Last updated: 2026-02-01*
*Owner: Agim Team*
