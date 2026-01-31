/*
 * Agim - Bytecode Format
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_VM_BYTECODE_H
#define AGIM_VM_BYTECODE_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#include "vm/value.h"

typedef struct InlineCache InlineCache;

/* Opcodes */

typedef enum Opcode {
    /* Stack operations */
    OP_NOP,
    OP_POP,
    OP_DUP,
    OP_DUP2,
    OP_SWAP,

    /* Constants */
    OP_CONST,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,

    /* Arithmetic */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_NEG,

    /* Comparison */
    OP_EQ,
    OP_NE,
    OP_LT,
    OP_LE,
    OP_GT,
    OP_GE,

    /* Logic */
    OP_NOT,
    OP_AND,
    OP_OR,

    /* Variables */
    OP_GET_LOCAL,
    OP_SET_LOCAL,
    OP_GET_GLOBAL,
    OP_SET_GLOBAL,

    /* Control flow */
    OP_JUMP,
    OP_JUMP_IF,
    OP_JUMP_UNLESS,
    OP_LOOP,

    /* Functions */
    OP_CALL,
    OP_RETURN,
    OP_CLOSURE,

    /* Data structures */
    OP_ARRAY_NEW,
    OP_ARRAY_PUSH,
    OP_ARRAY_GET,
    OP_ARRAY_SET,
    OP_MAP_NEW,
    OP_MAP_GET,
    OP_MAP_SET,
    OP_MAP_GET_IC,

    /* String */
    OP_CONCAT,

    /* Process operations */
    OP_SPAWN,
    OP_SEND,
    OP_RECEIVE,
    OP_RECEIVE_TIMEOUT,
    OP_SELF,
    OP_YIELD,

    /* Linking & Monitoring */
    OP_LINK,
    OP_UNLINK,
    OP_MONITOR,
    OP_DEMONITOR,

    /* Supervisor operations */
    OP_SUP_START,
    OP_SUP_ADD_CHILD,
    OP_SUP_REMOVE_CHILD,
    OP_SUP_WHICH_CHILDREN,
    OP_SUP_SHUTDOWN,

    /* Process groups */
    OP_GROUP_JOIN,
    OP_GROUP_LEAVE,
    OP_GROUP_SEND,
    OP_GROUP_SEND_OTHERS,
    OP_GROUP_MEMBERS,
    OP_GROUP_LIST,

    /* Telemetry & Introspection */
    OP_GET_STATS,
    OP_TRACE,
    OP_TRACE_OFF,

    /* Selective receive */
    OP_RECEIVE_MATCH,

    /* Agim primitives */
    OP_INFER,
    OP_TOOL_CALL,
    OP_MEMORY_GET,
    OP_MEMORY_SET,

    /* Utility */
    OP_LEN,
    OP_TYPE,
    OP_KEYS,
    OP_PUSH,
    OP_POP_ARRAY,
    OP_SLICE,
    OP_TO_STRING,
    OP_TO_INT,
    OP_TO_FLOAT,

    /* File I/O */
    OP_FILE_READ,
    OP_FILE_WRITE,
    OP_FILE_EXISTS,
    OP_FILE_LINES,
    OP_FILE_WRITE_BYTES,

    /* HTTP */
    OP_HTTP_GET,
    OP_HTTP_POST,
    OP_HTTP_PUT,
    OP_HTTP_DELETE,
    OP_HTTP_PATCH,
    OP_HTTP_REQUEST,

    /* Shell */
    OP_SHELL,

    /* JSON */
    OP_JSON_PARSE,
    OP_JSON_ENCODE,

    /* Environment */
    OP_ENV_GET,
    OP_ENV_SET,

    /* Time */
    OP_SLEEP,
    OP_TIME,
    OP_TIME_FORMAT,

    /* Random */
    OP_RANDOM,
    OP_RANDOM_INT,

    /* String operations */
    OP_SPLIT,
    OP_JOIN,
    OP_TRIM,
    OP_REPLACE,
    OP_CONTAINS,
    OP_STARTS_WITH,
    OP_ENDS_WITH,
    OP_UPPER,
    OP_LOWER,
    OP_CHAR_AT,
    OP_INDEX_OF,

    /* Base64 */
    OP_BASE64_ENCODE,
    OP_BASE64_DECODE,

    /* I/O */
    OP_READ_STDIN,
    OP_PRINT_ERR,

    /* Math */
    OP_FLOOR,
    OP_CEIL,
    OP_ROUND,
    OP_ABS,
    OP_SQRT,
    OP_POW,
    OP_MIN,
    OP_MAX,

    /* WebSocket */
    OP_WS_CONNECT,
    OP_WS_SEND,
    OP_WS_RECV,
    OP_WS_CLOSE,

    /* Streaming */
    OP_HTTP_STREAM,
    OP_STREAM_READ,
    OP_STREAM_CLOSE,

    /* Process */
    OP_EXEC,
    OP_EXEC_ASYNC,
    OP_PROC_WRITE,
    OP_PROC_READ,
    OP_PROC_CLOSE,

    /* UUID */
    OP_UUID,

    /* Hashing */
    OP_HASH_MD5,
    OP_HASH_SHA256,

    /* Debug */
    OP_PRINT,

    /* Result operations */
    OP_RESULT_OK,
    OP_RESULT_ERR,
    OP_RESULT_IS_OK,
    OP_RESULT_IS_ERR,
    OP_RESULT_UNWRAP,
    OP_RESULT_UNWRAP_OR,
    OP_RESULT_MATCH,

    /* Tool introspection */
    OP_LIST_TOOLS,
    OP_TOOL_SCHEMA,

    /* Option operations */
    OP_SOME,
    OP_NONE,
    OP_IS_SOME,
    OP_IS_NONE,
    OP_UNWRAP_OPTION,
    OP_UNWRAP_OPTION_OR,

    /* Struct operations */
    OP_STRUCT_NEW,
    OP_STRUCT_GET,
    OP_STRUCT_SET,
    OP_STRUCT_GET_INDEX,

    /* Enum operations */
    OP_ENUM_NEW,
    OP_ENUM_IS,
    OP_ENUM_PAYLOAD,

    /* End */
    OP_HALT,
} Opcode;

/* Bytecode Chunk */

typedef struct Chunk {
    uint8_t *code;
    size_t code_size;
    size_t code_capacity;

    Value **constants;
    size_t constants_size;
    size_t constants_capacity;

    InlineCache *ic_slots;
    size_t ic_count;
    size_t ic_capacity;

    int *lines;
    size_t lines_capacity;
} Chunk;

/* Tool Metadata */

typedef struct ToolParamMeta {
    char *name;
    char *type;
    char *description;
} ToolParamMeta;

typedef struct ToolInfo {
    char *name;
    char *description;
    ToolParamMeta *params;
    size_t param_count;
    char *return_type;
    size_t func_index;
} ToolInfo;

/* Bytecode Container */

typedef struct Bytecode {
    _Atomic(uint32_t) refcount;

    Chunk *main;

    Chunk **functions;
    size_t functions_count;
    size_t functions_capacity;

    char **strings;
    size_t strings_count;
    size_t strings_capacity;

    ToolInfo *tools;
    size_t tools_count;
    size_t tools_capacity;

    char *source_name;
    uint32_t version;
} Bytecode;

/* Chunk API */

Chunk *chunk_new(void);
void chunk_free(Chunk *chunk);

void chunk_write_byte(Chunk *chunk, uint8_t byte, int line);
void chunk_write_opcode(Chunk *chunk, Opcode op, int line);
size_t chunk_write_arg(Chunk *chunk, uint16_t arg, int line);
size_t chunk_write_jump(Chunk *chunk, Opcode op, int line);
void chunk_patch_jump(Chunk *chunk, size_t offset);

size_t chunk_add_constant(Chunk *chunk, Value *value);
size_t chunk_alloc_ic(Chunk *chunk);

uint8_t chunk_read_byte(Chunk *chunk, size_t offset);
uint16_t chunk_read_arg(Chunk *chunk, size_t offset);

/* Bytecode API */

Bytecode *bytecode_new(void);
void bytecode_free(Bytecode *code);
Bytecode *bytecode_retain(Bytecode *code);
void bytecode_release(Bytecode *code);

size_t bytecode_add_function(Bytecode *code, Chunk *chunk);
size_t bytecode_add_string(Bytecode *code, const char *str);
const char *bytecode_get_string(Bytecode *code, size_t index);

size_t bytecode_add_tool(Bytecode *code, const char *name, size_t func_index,
                         const char **param_names, const char **param_types,
                         const char **param_descriptions, size_t param_count,
                         const char *return_type, const char *description);
const ToolInfo *bytecode_get_tools(Bytecode *code, size_t *count);
const ToolInfo *bytecode_find_tool(Bytecode *code, const char *name);

uint8_t *bytecode_serialize(const Bytecode *code, size_t *size);
Bytecode *bytecode_deserialize(const uint8_t *data, size_t size);

void chunk_disassemble(Chunk *chunk, const char *name);
size_t chunk_disassemble_instruction(Chunk *chunk, size_t offset);

#endif /* AGIM_VM_BYTECODE_H */
