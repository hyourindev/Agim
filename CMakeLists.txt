cmake_minimum_required(VERSION 3.16)

project(agim
    VERSION 0.1.0
    DESCRIPTION "A language and runtime for building isolated AI agents"
    LANGUAGES C
)

# C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags - security-focused warnings for production readiness
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat=2 -Wformat-security")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wnull-dereference")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Warray-bounds")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wimplicit-fallthrough")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wpointer-arith -Wwrite-strings")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wstrict-prototypes -Wold-style-definition")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong")

# GCC-specific warnings
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wlogical-op -Wduplicated-cond")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wformat-overflow=2 -Wshift-overflow=2")
endif()
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DAGIM_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")

# Options
option(AGIM_BUILD_TESTS "Build test suite" ON)
option(AGIM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(AGIM_ENABLE_MSAN "Enable MemorySanitizer (Clang only)" OFF)
option(AGIM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(AGIM_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(AGIM_ENABLE_COVERAGE "Enable code coverage" OFF)
option(AGIM_ENABLE_FUZZING "Enable libFuzzer (Clang only)" OFF)
option(AGIM_ENABLE_PGO_GENERATE "Enable Profile-Guided Optimization (generate phase)" OFF)
option(AGIM_ENABLE_PGO_USE "Enable Profile-Guided Optimization (use phase)" OFF)

# Sanitizers - ASan (Address Sanitizer)
if(AGIM_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# Sanitizers - MSan (Memory Sanitizer) - Clang only
if(AGIM_ENABLE_MSAN)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=memory -fno-omit-frame-pointer")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize-memory-track-origins=2")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=memory")
    else()
        message(WARNING "MSan is only supported with Clang. Ignoring AGIM_ENABLE_MSAN.")
    endif()
endif()

# Sanitizers - UBSan (Undefined Behavior Sanitizer)
if(AGIM_ENABLE_UBSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=float-divide-by-zero")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=float-cast-overflow")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif()

# Sanitizers - TSan (Thread Sanitizer)
if(AGIM_ENABLE_TSAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=thread -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Code coverage (gcov/lcov compatible)
if(AGIM_ENABLE_COVERAGE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --coverage -fprofile-arcs -ftest-coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Fuzzing (libFuzzer - Clang only)
if(AGIM_ENABLE_FUZZING)
    if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=fuzzer-no-link,address -fno-omit-frame-pointer")
        set(AGIM_FUZZER_LINK_FLAGS "-fsanitize=fuzzer,address")
        message(STATUS "Fuzzing enabled with libFuzzer and ASan")
    else()
        message(FATAL_ERROR "Fuzzing requires Clang. Please set CC=clang and CXX=clang++")
    endif()
endif()

# Profile-Guided Optimization
if(AGIM_ENABLE_PGO_GENERATE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-generate")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-generate")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-instr-generate")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-generate")
    endif()
endif()

if(AGIM_ENABLE_PGO_USE)
    if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-use -fprofile-correction")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-use")
    elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-instr-use=default.profdata")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-instr-use=default.profdata")
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Utility library
add_library(agim_util STATIC
    src/util/alloc.c
    src/util/hash.c
    src/util/pool.c
    src/util/worker_alloc.c
)
target_include_directories(agim_util PUBLIC src)

# Types library (object library to handle circular dependency with VM)
add_library(agim_types OBJECT
    src/types/string.c
    src/types/array.c
    src/types/map.c
    src/types/vector.c
    src/types/closure.c
)
target_include_directories(agim_types PUBLIC src)

# VM library (consolidated due to circular dependencies)
add_library(agim_vm STATIC
    # VM core
    src/vm/value.c
    src/vm/bytecode.c
    src/vm/vm.c
    src/vm/gc.c
    src/vm/ic.c
    src/vm/regvm.c
    src/vm/sandbox.c
    src/debug/debug.c
    src/debug/trace.c
    src/debug/log.c
    src/debug/metrics.c
    src/debug/health.c
    src/vm/primitives.c
    # Runtime
    src/runtime/mailbox.c
    src/runtime/capability.c
    src/runtime/block.c
    src/runtime/scheduler.c
    src/runtime/worker.c
    src/runtime/supervisor.c
    src/runtime/timer.c
    src/runtime/serialize.c
    src/runtime/checkpoint.c
    # Distribution
    src/dist/node.c
    # Hot Code Reloading
    src/runtime/hotreload.c
    # Process Groups & Telemetry
    src/runtime/procgroup.c
    src/runtime/telemetry.c
    # Builtin
    src/builtin/inference.c
    src/builtin/tools.c
    src/builtin/memory.c
    # Types (included to resolve circular dependency)
    $<TARGET_OBJECTS:agim_types>
)
target_include_directories(agim_vm PUBLIC src)
target_link_libraries(agim_vm PUBLIC agim_util pthread m)

# Agim compiler library
add_library(agim_lang STATIC
    src/lang/lexer.c
    src/lang/ast.c
    src/lang/parser.c
    src/lang/typechecker.c
    src/lang/compiler.c
    src/lang/module.c
    src/lang/regalloc.c
    src/lang/regcompiler.c
    src/lang/agim.c
)
target_include_directories(agim_lang PUBLIC src)
target_link_libraries(agim_lang PUBLIC agim_vm)

# Tests
if(AGIM_BUILD_TESTS)
    enable_testing()

    add_executable(test_value tests/vm/test_value.c)
    target_link_libraries(test_value agim_vm)
    add_test(NAME test_value COMMAND test_value)

    add_executable(test_bytecode tests/vm/test_bytecode.c)
    target_link_libraries(test_bytecode agim_vm)
    add_test(NAME test_bytecode COMMAND test_bytecode)

    add_executable(test_bytecode_validation tests/vm/test_bytecode_validation.c)
    target_link_libraries(test_bytecode_validation agim_vm)
    add_test(NAME test_bytecode_validation COMMAND test_bytecode_validation)

    add_executable(test_vm tests/vm/test_vm.c)
    target_link_libraries(test_vm agim_vm)
    add_test(NAME test_vm COMMAND test_vm)

    add_executable(test_gc tests/vm/test_gc.c)
    target_link_libraries(test_gc agim_vm)
    add_test(NAME test_gc COMMAND test_gc)

    add_executable(test_block tests/vm/test_block.c)
    target_link_libraries(test_block agim_vm)
    add_test(NAME test_block COMMAND test_block)

    add_executable(test_scheduler tests/vm/test_scheduler.c)
    target_link_libraries(test_scheduler agim_vm)
    add_test(NAME test_scheduler COMMAND test_scheduler)

    add_executable(test_message tests/vm/test_message.c)
    target_link_libraries(test_message agim_vm)
    add_test(NAME test_message COMMAND test_message)

    add_executable(test_mailbox_concurrent tests/vm/test_mailbox_concurrent.c)
    target_link_libraries(test_mailbox_concurrent agim_vm)
    add_test(NAME test_mailbox_concurrent COMMAND test_mailbox_concurrent)

    add_executable(test_integration tests/vm/test_integration.c)
    target_link_libraries(test_integration agim_vm)
    add_test(NAME test_integration COMMAND test_integration)

    add_executable(test_integration_linking tests/vm/test_integration_linking.c)
    target_link_libraries(test_integration_linking agim_vm)
    add_test(NAME test_integration_linking COMMAND test_integration_linking)

    add_executable(test_integration_monitoring tests/vm/test_integration_monitoring.c)
    target_link_libraries(test_integration_monitoring agim_vm)
    add_test(NAME test_integration_monitoring COMMAND test_integration_monitoring)

    add_executable(test_parallel tests/vm/test_parallel.c)
    target_link_libraries(test_parallel agim_vm)
    add_test(NAME test_parallel COMMAND test_parallel)

    add_executable(test_cow tests/vm/test_cow.c)
    target_link_libraries(test_cow agim_vm)
    add_test(NAME test_cow COMMAND test_cow)

    add_executable(test_ic tests/vm/test_ic.c)
    target_link_libraries(test_ic agim_vm)
    add_test(NAME test_ic COMMAND test_ic)

    add_executable(test_regvm tests/vm/test_regvm.c)
    target_link_libraries(test_regvm agim_vm)
    add_test(NAME test_regvm COMMAND test_regvm)

    # Comprehensive test suites (Phase 1: Testing Infrastructure)
    add_executable(test_vm_edge_cases tests/vm/test_vm_edge_cases.c)
    target_link_libraries(test_vm_edge_cases agim_vm)
    add_test(NAME test_vm_edge_cases COMMAND test_vm_edge_cases)

    add_executable(test_gc_comprehensive tests/vm/test_gc_comprehensive.c)
    target_link_libraries(test_gc_comprehensive agim_vm)
    add_test(NAME test_gc_comprehensive COMMAND test_gc_comprehensive)

    add_executable(test_scheduler_comprehensive tests/vm/test_scheduler_comprehensive.c)
    target_link_libraries(test_scheduler_comprehensive agim_vm)
    add_test(NAME test_scheduler_comprehensive COMMAND test_scheduler_comprehensive)

    add_executable(test_vm_stack_operations tests/vm/test_vm_stack_operations.c)
    target_link_libraries(test_vm_stack_operations agim_vm)
    add_test(NAME test_vm_stack_operations COMMAND test_vm_stack_operations)

    add_executable(test_vm_arithmetic tests/vm/test_vm_arithmetic.c)
    target_link_libraries(test_vm_arithmetic agim_vm)
    add_test(NAME test_vm_arithmetic COMMAND test_vm_arithmetic)

    add_executable(test_vm_comparison tests/vm/test_vm_comparison.c)
    target_link_libraries(test_vm_comparison agim_vm)
    add_test(NAME test_vm_comparison COMMAND test_vm_comparison)

    add_executable(test_vm_control_flow tests/vm/test_vm_control_flow.c)
    target_link_libraries(test_vm_control_flow agim_vm)
    add_test(NAME test_vm_control_flow COMMAND test_vm_control_flow)

    add_executable(test_vm_functions tests/vm/test_vm_functions.c)
    target_link_libraries(test_vm_functions agim_vm)
    add_test(NAME test_vm_functions COMMAND test_vm_functions)

    add_executable(test_vm_memory tests/vm/test_vm_memory.c)
    target_link_libraries(test_vm_memory agim_vm)
    add_test(NAME test_vm_memory COMMAND test_vm_memory)

    add_executable(test_vm_process tests/vm/test_vm_process.c)
    target_link_libraries(test_vm_process agim_vm)
    add_test(NAME test_vm_process COMMAND test_vm_process)

    add_executable(test_gc_allocation tests/vm/test_gc_allocation.c)
    target_link_libraries(test_gc_allocation agim_vm)
    add_test(NAME test_gc_allocation COMMAND test_gc_allocation)

    add_executable(test_gc_marking tests/vm/test_gc_marking.c)
    target_link_libraries(test_gc_marking agim_vm)
    add_test(NAME test_gc_marking COMMAND test_gc_marking)

    add_executable(test_gc_sweeping tests/vm/test_gc_sweeping.c)
    target_link_libraries(test_gc_sweeping agim_vm)
    add_test(NAME test_gc_sweeping COMMAND test_gc_sweeping)

    add_executable(test_gc_generational tests/vm/test_gc_generational.c)
    target_link_libraries(test_gc_generational agim_vm)
    add_test(NAME test_gc_generational COMMAND test_gc_generational)

    add_executable(test_gc_concurrent tests/vm/test_gc_concurrent.c)
    target_link_libraries(test_gc_concurrent agim_vm)
    add_test(NAME test_gc_concurrent COMMAND test_gc_concurrent)

    add_executable(test_scheduler_lifecycle tests/vm/test_scheduler_lifecycle.c)
    target_link_libraries(test_scheduler_lifecycle agim_vm)
    add_test(NAME test_scheduler_lifecycle COMMAND test_scheduler_lifecycle)

    add_executable(test_scheduler_spawn tests/vm/test_scheduler_spawn.c)
    target_link_libraries(test_scheduler_spawn agim_vm)
    add_test(NAME test_scheduler_spawn COMMAND test_scheduler_spawn)

    add_executable(test_scheduler_registry tests/vm/test_scheduler_registry.c)
    target_link_libraries(test_scheduler_registry agim_vm)
    add_test(NAME test_scheduler_registry COMMAND test_scheduler_registry)

    add_executable(test_scheduler_runqueue tests/vm/test_scheduler_runqueue.c)
    target_link_libraries(test_scheduler_runqueue agim_vm)
    add_test(NAME test_scheduler_runqueue COMMAND test_scheduler_runqueue)

    add_executable(test_scheduler_execution tests/vm/test_scheduler_execution.c)
    target_link_libraries(test_scheduler_execution agim_vm)
    add_test(NAME test_scheduler_execution COMMAND test_scheduler_execution)

    add_executable(test_scheduler_exit tests/vm/test_scheduler_exit.c)
    target_link_libraries(test_scheduler_exit agim_vm)
    add_test(NAME test_scheduler_exit COMMAND test_scheduler_exit)

    add_executable(test_block_lifecycle tests/vm/test_block_lifecycle.c)
    target_link_libraries(test_block_lifecycle agim_vm)
    add_test(NAME test_block_lifecycle COMMAND test_block_lifecycle)

    add_executable(test_block_capabilities tests/vm/test_block_capabilities.c)
    target_link_libraries(test_block_capabilities agim_vm)
    add_test(NAME test_block_capabilities COMMAND test_block_capabilities)

    add_executable(test_block_linking tests/vm/test_block_linking.c)
    target_link_libraries(test_block_linking agim_vm)
    add_test(NAME test_block_linking COMMAND test_block_linking)

    add_executable(test_block_monitoring tests/vm/test_block_monitoring.c)
    target_link_libraries(test_block_monitoring agim_vm)
    add_test(NAME test_block_monitoring COMMAND test_block_monitoring)

    add_executable(test_block_messaging tests/vm/test_block_messaging.c)
    target_link_libraries(test_block_messaging agim_vm)
    add_test(NAME test_block_messaging COMMAND test_block_messaging)

    add_executable(test_worker_deque tests/vm/test_worker_deque.c)
    target_link_libraries(test_worker_deque agim_vm)
    add_test(NAME test_worker_deque COMMAND test_worker_deque)

    add_executable(test_worker_lifecycle tests/vm/test_worker_lifecycle.c)
    target_link_libraries(test_worker_lifecycle agim_vm)
    add_test(NAME test_worker_lifecycle COMMAND test_worker_lifecycle)

    add_executable(test_worker_stealing tests/vm/test_worker_stealing.c)
    target_link_libraries(test_worker_stealing agim_vm)
    add_test(NAME test_worker_stealing COMMAND test_worker_stealing)

    add_executable(test_worker_execution tests/vm/test_worker_execution.c)
    target_link_libraries(test_worker_execution agim_vm)
    add_test(NAME test_worker_execution COMMAND test_worker_execution)

    add_executable(test_timer_wheel tests/vm/test_timer_wheel.c)
    target_link_libraries(test_timer_wheel agim_vm)
    add_test(NAME test_timer_wheel COMMAND test_timer_wheel)

    add_executable(test_value_refcount tests/vm/test_value_refcount.c)
    target_link_libraries(test_value_refcount agim_vm)
    add_test(NAME test_value_refcount COMMAND test_value_refcount)

    add_executable(test_value_nanbox tests/vm/test_value_nanbox.c)
    target_link_libraries(test_value_nanbox agim_vm)
    add_test(NAME test_value_nanbox COMMAND test_value_nanbox)

    # Language tests
    add_executable(test_lexer tests/lang/test_lexer.c)
    target_link_libraries(test_lexer agim_lang)
    add_test(NAME test_lexer COMMAND test_lexer)

    add_executable(test_parser tests/lang/test_parser.c)
    target_link_libraries(test_parser agim_lang)
    add_test(NAME test_parser COMMAND test_parser)

    add_executable(test_compiler tests/lang/test_compiler.c)
    target_link_libraries(test_compiler agim_lang)
    add_test(NAME test_compiler COMMAND test_compiler)

    add_executable(test_programs tests/lang/test_programs.c)
    target_link_libraries(test_programs agim_lang)
    add_test(NAME test_programs COMMAND test_programs)

    add_executable(test_regcompiler tests/lang/test_regcompiler.c)
    target_link_libraries(test_regcompiler agim_lang)
    add_test(NAME test_regcompiler COMMAND test_regcompiler)

    # Feature tests (modules, results, tools)
    add_executable(test_features tests/lang/test_features.c)
    target_link_libraries(test_features agim_lang)
    add_test(NAME test_features COMMAND test_features)

    # Type system unit tests (Option, Result, Struct, Enum)
    add_executable(test_types tests/vm/test_types.c)
    target_link_libraries(test_types agim_vm)
    add_test(NAME test_types COMMAND test_types)

    # Type operations tests
    add_executable(test_array_operations tests/types/test_array_operations.c)
    target_link_libraries(test_array_operations agim_vm)
    add_test(NAME test_array_operations COMMAND test_array_operations)

    add_executable(test_map_operations tests/types/test_map_operations.c)
    target_link_libraries(test_map_operations agim_vm)
    add_test(NAME test_map_operations COMMAND test_map_operations)

    add_executable(test_string_operations tests/types/test_string_operations.c)
    target_link_libraries(test_string_operations agim_vm)
    add_test(NAME test_string_operations COMMAND test_string_operations)

    add_executable(test_string_interning tests/types/test_string_interning.c)
    target_link_libraries(test_string_interning agim_vm)
    add_test(NAME test_string_interning COMMAND test_string_interning)

    add_executable(test_closure_operations tests/types/test_closure_operations.c)
    target_link_libraries(test_closure_operations agim_vm)
    add_test(NAME test_closure_operations COMMAND test_closure_operations)

    # File operations tests (sandbox-based I/O)
    add_executable(test_file_operations tests/vm/test_file_operations.c)
    target_link_libraries(test_file_operations agim_vm)
    add_test(NAME test_file_operations COMMAND test_file_operations)

    # Type checker tests
    add_executable(test_typechecker tests/lang/test_typechecker.c)
    target_link_libraries(test_typechecker agim_lang)
    add_test(NAME test_typechecker COMMAND test_typechecker)

    # Typed programs integration tests
    add_executable(test_typed_programs tests/lang/test_typed_programs.c)
    target_link_libraries(test_typed_programs agim_lang)
    add_test(NAME test_typed_programs COMMAND test_typed_programs)

    # Security tests
    add_executable(test_security tests/security/test_security.c)
    target_link_libraries(test_security agim_lang)
    add_test(NAME test_security COMMAND test_security)

    add_executable(test_capabilities tests/security/test_capabilities.c)
    target_link_libraries(test_capabilities agim_vm)
    add_test(NAME test_capabilities COMMAND test_capabilities)

    add_executable(test_buffer_overflow tests/security/test_buffer_overflow.c)
    target_link_libraries(test_buffer_overflow agim_vm)
    add_test(NAME test_buffer_overflow COMMAND test_buffer_overflow)

    add_executable(test_integer_overflow tests/security/test_integer_overflow.c)
    target_link_libraries(test_integer_overflow agim_vm)
    add_test(NAME test_integer_overflow COMMAND test_integer_overflow)

    add_executable(test_use_after_free tests/security/test_use_after_free.c)
    target_link_libraries(test_use_after_free agim_vm)
    add_test(NAME test_use_after_free COMMAND test_use_after_free)

    # Debug/Observability tests
    add_executable(test_metrics tests/debug/test_metrics.c)
    target_link_libraries(test_metrics agim_vm)
    add_test(NAME test_metrics COMMAND test_metrics)

    add_executable(test_health tests/debug/test_health.c)
    target_link_libraries(test_health agim_vm)
    add_test(NAME test_health COMMAND test_health)

    # End-to-end tests for Erlang-like VM features
    add_executable(test_e2e_lifecycle tests/e2e/test_e2e_lifecycle.c)
    target_link_libraries(test_e2e_lifecycle agim_vm)
    add_test(NAME test_e2e_lifecycle COMMAND test_e2e_lifecycle)

    add_executable(test_e2e_messaging tests/e2e/test_e2e_messaging.c)
    target_link_libraries(test_e2e_messaging agim_vm)
    add_test(NAME test_e2e_messaging COMMAND test_e2e_messaging)

    add_executable(test_e2e_supervision tests/e2e/test_e2e_supervision.c)
    target_link_libraries(test_e2e_supervision agim_vm)
    add_test(NAME test_e2e_supervision COMMAND test_e2e_supervision)

    add_executable(test_e2e_hotreload tests/e2e/test_e2e_hotreload.c)
    target_link_libraries(test_e2e_hotreload agim_vm)
    add_test(NAME test_e2e_hotreload COMMAND test_e2e_hotreload)

    add_executable(test_e2e_checkpoint tests/e2e/test_e2e_checkpoint.c)
    target_link_libraries(test_e2e_checkpoint agim_vm)
    add_test(NAME test_e2e_checkpoint COMMAND test_e2e_checkpoint)

    add_executable(test_e2e_distributed tests/e2e/test_e2e_distributed.c)
    target_link_libraries(test_e2e_distributed agim_vm)
    add_test(NAME test_e2e_distributed COMMAND test_e2e_distributed)

    add_executable(test_e2e_timer tests/e2e/test_e2e_timer.c)
    target_link_libraries(test_e2e_timer agim_vm)
    add_test(NAME test_e2e_timer COMMAND test_e2e_timer)

    add_executable(test_e2e_scheduler tests/e2e/test_e2e_scheduler.c)
    target_link_libraries(test_e2e_scheduler agim_vm)
    add_test(NAME test_e2e_scheduler COMMAND test_e2e_scheduler)

    add_executable(test_e2e_echo_server tests/e2e/test_e2e_echo_server.c)
    target_link_libraries(test_e2e_echo_server agim_vm)
    add_test(NAME test_e2e_echo_server COMMAND test_e2e_echo_server)

    add_executable(test_e2e_worker_pool tests/e2e/test_e2e_worker_pool.c)
    target_link_libraries(test_e2e_worker_pool agim_vm)
    add_test(NAME test_e2e_worker_pool COMMAND test_e2e_worker_pool)

    add_executable(test_e2e_pubsub tests/e2e/test_e2e_pubsub.c)
    target_link_libraries(test_e2e_pubsub agim_vm)
    add_test(NAME test_e2e_pubsub COMMAND test_e2e_pubsub)

    add_executable(test_e2e_state_machine tests/e2e/test_e2e_state_machine.c)
    target_link_libraries(test_e2e_state_machine agim_vm)
    add_test(NAME test_e2e_state_machine COMMAND test_e2e_state_machine)

    add_executable(test_e2e_pipeline tests/e2e/test_e2e_pipeline.c)
    target_link_libraries(test_e2e_pipeline agim_vm)
    add_test(NAME test_e2e_pipeline COMMAND test_e2e_pipeline)

    # Property-based tests
    add_executable(test_array_properties tests/property/test_array_properties.c)
    target_include_directories(test_array_properties PRIVATE tests/property)
    target_link_libraries(test_array_properties agim_vm)
    add_test(NAME test_array_properties COMMAND test_array_properties)

    add_executable(test_map_properties tests/property/test_map_properties.c)
    target_include_directories(test_map_properties PRIVATE tests/property)
    target_link_libraries(test_map_properties agim_vm)
    add_test(NAME test_map_properties COMMAND test_map_properties)

    add_executable(test_scheduler_properties tests/property/test_scheduler_properties.c)
    target_include_directories(test_scheduler_properties PRIVATE tests/property)
    target_link_libraries(test_scheduler_properties agim_vm)
    add_test(NAME test_scheduler_properties COMMAND test_scheduler_properties)

    add_executable(test_gc_properties tests/property/test_gc_properties.c)
    target_include_directories(test_gc_properties PRIVATE tests/property)
    target_link_libraries(test_gc_properties agim_vm)
    add_test(NAME test_gc_properties COMMAND test_gc_properties)

    add_executable(test_string_properties tests/property/test_string_properties.c)
    target_include_directories(test_string_properties PRIVATE tests/property)
    target_link_libraries(test_string_properties agim_vm)
    add_test(NAME test_string_properties COMMAND test_string_properties)
endif()

# Agim CLI
add_executable(agim cmd/agim.c)
target_link_libraries(agim agim_lang)

# Benchmarks
add_executable(agim_bench bench/benchmark.c)
target_link_libraries(agim_bench agim_vm)

add_executable(agim_regbench bench/regbench.c)
target_link_libraries(agim_regbench agim_lang)

add_executable(agim_parallel bench/parallel_bench.c)
target_link_libraries(agim_parallel agim_vm)

add_executable(agim_spawn_bench bench/spawn_bench.c)
target_link_libraries(agim_spawn_bench agim_vm)

add_executable(agim_gc_bench bench/gc_bench.c)
target_link_libraries(agim_gc_bench agim_vm)

add_executable(agim_mailbox_bench bench/mailbox_bench.c)
target_link_libraries(agim_mailbox_bench agim_vm)

add_executable(agim_skynet_bench bench/skynet_bench.c)
target_link_libraries(agim_skynet_bench agim_vm)

add_executable(agim_ring_bench bench/ring_bench.c)
target_link_libraries(agim_ring_bench agim_vm)

add_executable(agim_io_bench bench/io_bench.c)

# Fuzz targets (libFuzzer - Clang only)
if(AGIM_ENABLE_FUZZING)
    # Lexer fuzzer
    add_executable(fuzz_lexer fuzz/fuzz_lexer.c)
    target_link_libraries(fuzz_lexer agim_lang ${AGIM_FUZZER_LINK_FLAGS})
    target_link_options(fuzz_lexer PRIVATE ${AGIM_FUZZER_LINK_FLAGS})

    # Parser fuzzer
    add_executable(fuzz_parser fuzz/fuzz_parser.c)
    target_link_libraries(fuzz_parser agim_lang ${AGIM_FUZZER_LINK_FLAGS})
    target_link_options(fuzz_parser PRIVATE ${AGIM_FUZZER_LINK_FLAGS})

    # Bytecode fuzzer
    add_executable(fuzz_bytecode fuzz/fuzz_bytecode.c)
    target_link_libraries(fuzz_bytecode agim_vm ${AGIM_FUZZER_LINK_FLAGS})
    target_link_options(fuzz_bytecode PRIVATE ${AGIM_FUZZER_LINK_FLAGS})

    # NaN-box fuzzer
    add_executable(fuzz_nanbox fuzz/fuzz_nanbox.c)
    target_link_libraries(fuzz_nanbox agim_vm ${AGIM_FUZZER_LINK_FLAGS})
    target_link_options(fuzz_nanbox PRIVATE ${AGIM_FUZZER_LINK_FLAGS})

    # JSON fuzzer
    add_executable(fuzz_json fuzz/fuzz_json.c)
    target_link_libraries(fuzz_json agim_vm ${AGIM_FUZZER_LINK_FLAGS})
    target_link_options(fuzz_json PRIVATE ${AGIM_FUZZER_LINK_FLAGS})
endif()
