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

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DAGIM_DEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -march=native -flto -DNDEBUG")

# Options
option(AGIM_BUILD_TESTS "Build test suite" ON)
option(AGIM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(AGIM_ENABLE_PGO_GENERATE "Enable Profile-Guided Optimization (generate phase)" OFF)
option(AGIM_ENABLE_PGO_USE "Enable Profile-Guided Optimization (use phase)" OFF)

# Sanitizers
if(AGIM_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
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

    add_executable(test_integration tests/vm/test_integration.c)
    target_link_libraries(test_integration agim_vm)
    add_test(NAME test_integration COMMAND test_integration)

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

    # Typed programs integration tests
    add_executable(test_typed_programs tests/lang/test_typed_programs.c)
    target_link_libraries(test_typed_programs agim_lang)
    add_test(NAME test_typed_programs COMMAND test_typed_programs)

    # Security tests
    add_executable(test_security tests/security/test_security.c)
    target_link_libraries(test_security agim_lang)
    add_test(NAME test_security COMMAND test_security)

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
