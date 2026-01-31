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

# Sanitizers
if(AGIM_ENABLE_ASAN)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Export compile commands for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# BearSSL library (vendored TLS implementation)
file(GLOB BEARSSL_SOURCES
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/aead/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/codec/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/ec/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/hash/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/int/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/kdf/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/mac/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/rand/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/rsa/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/ssl/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/symcipher/*.c
    ${CMAKE_SOURCE_DIR}/vendor/bearssl/src/x509/*.c
)
add_library(bearssl STATIC ${BEARSSL_SOURCES})
target_include_directories(bearssl
    PUBLIC ${CMAKE_SOURCE_DIR}/vendor/bearssl/inc
    PRIVATE ${CMAKE_SOURCE_DIR}/vendor/bearssl/src
)
# Suppress warnings from vendored code
target_compile_options(bearssl PRIVATE -w)

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
    # Networking
    src/net/url.c
    src/net/tcp.c
    src/net/tls.c
    src/net/http_parser.c
    src/net/http.c
    src/net/sse.c
    src/net/websocket.c
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
target_include_directories(agim_vm PUBLIC src ${CMAKE_SOURCE_DIR}/vendor/bearssl/inc)
target_link_libraries(agim_vm PUBLIC agim_util bearssl pthread m)

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

    # Network tests
    add_executable(test_tls tests/net/test_tls.c)
    target_link_libraries(test_tls agim_vm)
    add_test(NAME test_tls COMMAND test_tls)

    add_executable(test_streaming tests/net/test_streaming.c)
    target_link_libraries(test_streaming agim_vm)
    add_test(NAME test_streaming COMMAND test_streaming)

    add_executable(test_websocket tests/net/test_websocket.c)
    target_link_libraries(test_websocket agim_vm)
    add_test(NAME test_websocket COMMAND test_websocket)
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
