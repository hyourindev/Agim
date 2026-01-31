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

/* Forward declaration for inline cache */
typedef struct InlineCache InlineCache;

/*============================================================================
 * Opcodes
 *============================================================================*/

typedef enum Opcode {
    /* Stack operations */
    OP_NOP,          /* No operation */
    OP_POP,          /* Pop top of stack */
    OP_DUP,          /* Duplicate top of stack */
    OP_SWAP,         /* Swap top two values */

    /* Constants */
    OP_CONST,        /* Push constant: arg = constant index */
    OP_NIL,          /* Push nil */
    OP_TRUE,         /* Push true */
    OP_FALSE,        /* Push false */

    /* Arithmetic */
    OP_ADD,          /* a + b */
    OP_SUB,          /* a - b */
    OP_MUL,          /* a * b */
    OP_DIV,          /* a / b */
    OP_MOD,          /* a % b */
    OP_NEG,          /* -a */

    /* Comparison */
    OP_EQ,           /* a == b */
    OP_NE,           /* a != b */
    OP_LT,           /* a < b */
    OP_LE,           /* a <= b */
    OP_GT,           /* a > b */
    OP_GE,           /* a >= b */

    /* Logic */
    OP_NOT,          /* !a */
    OP_AND,          /* a && b (short-circuit) */
    OP_OR,           /* a || b (short-circuit) */

    /* Variables */
    OP_GET_LOCAL,    /* Push local variable: arg = slot */
    OP_SET_LOCAL,    /* Set local variable: arg = slot */
    OP_GET_GLOBAL,   /* Push global: arg = name index */
    OP_SET_GLOBAL,   /* Set global: arg = name index */

    /* Control flow */
    OP_JUMP,         /* Unconditional jump: arg = offset */
    OP_JUMP_IF,      /* Jump if true: arg = offset */
    OP_JUMP_UNLESS,  /* Jump if false: arg = offset */
    OP_LOOP,         /* Jump backward: arg = offset */

    /* Functions */
    OP_CALL,         /* Call function: arg = arity */
    OP_RETURN,       /* Return from function */
    OP_CLOSURE,      /* Create closure: arg = function index */

    /* Data structures */
    OP_ARRAY_NEW,    /* Create empty array */
    OP_ARRAY_PUSH,   /* Push value to array */
    OP_ARRAY_GET,    /* Get array[index] */
    OP_ARRAY_SET,    /* Set array[index] = value */
    OP_MAP_NEW,      /* Create empty map */
    OP_MAP_GET,      /* Get map[key] */
    OP_MAP_SET,      /* Set map[key] = value */
    OP_MAP_GET_IC,   /* Get map[key] with inline cache: [key_idx:16][ic_slot:16] */

    /* String */
    OP_CONCAT,       /* String concatenation */

    /* Process operations (Agim-specific) */
    OP_SPAWN,        /* Spawn new block */
    OP_SEND,         /* Send message to block */
    OP_RECEIVE,      /* Receive message (blocking) */
    OP_RECEIVE_TIMEOUT, /* Receive with timeout: [timeout_ms on stack] -> Result<msg, timeout> */
    OP_SELF,         /* Get current block's PID */
    OP_YIELD,        /* Yield execution */

    /* Linking & Monitoring */
    OP_LINK,         /* Link to another block (bidirectional crash notification) */
    OP_UNLINK,       /* Unlink from another block */
    OP_MONITOR,      /* Monitor another block (unidirectional down notification) */
    OP_DEMONITOR,    /* Stop monitoring another block */

    /* Supervisor operations */
    OP_SUP_START,    /* Start a supervisor: [strategy on stack] -> supervisor_ok */
    OP_SUP_ADD_CHILD,/* Add child to supervisor: [name, code, restart_strategy] -> pid */
    OP_SUP_REMOVE_CHILD, /* Remove child: [name] -> ok */
    OP_SUP_WHICH_CHILDREN, /* List supervised children -> array */
    OP_SUP_SHUTDOWN, /* Shutdown supervisor -> ok */

    /* Process groups */
    OP_GROUP_JOIN,   /* Join a process group: [name] -> ok */
    OP_GROUP_LEAVE,  /* Leave a process group: [name] -> ok */
    OP_GROUP_SEND,   /* Send to all group members: [name, message] -> count */
    OP_GROUP_SEND_OTHERS, /* Send to group except self: [name, message] -> count */
    OP_GROUP_MEMBERS,/* Get group members: [name] -> array of pids */
    OP_GROUP_LIST,   /* List all groups: [] -> array of names */

    /* Telemetry & Introspection */
    OP_GET_STATS,    /* Get block statistics: [pid] -> stats map */
    OP_TRACE,        /* Enable tracing: [pid, flags] -> ok */
    OP_TRACE_OFF,    /* Disable tracing: [pid] -> ok */

    /* Selective receive (pattern matching) */
    OP_RECEIVE_MATCH,/* Receive with pattern: [pattern] -> matched message or nil */

    /* Agim primitives */
    OP_INFER,        /* LLM inference call */
    OP_TOOL_CALL,    /* Call built-in tool */
    OP_MEMORY_GET,   /* Get from persistent memory */
    OP_MEMORY_SET,   /* Set in persistent memory */

    /* Utility */
    OP_LEN,          /* Get length of array/string/map */
    OP_TYPE,         /* Get type name of value */
    OP_KEYS,         /* Get keys of map as array */
    OP_PUSH,         /* Push value to array */
    OP_POP_ARRAY,    /* Pop value from array */
    OP_SLICE,        /* Slice array/string */
    OP_TO_STRING,    /* Convert to string */
    OP_TO_INT,       /* Convert to int */
    OP_TO_FLOAT,     /* Convert to float */

    /* File I/O */
    OP_FILE_READ,    /* Read file contents */
    OP_FILE_WRITE,   /* Write to file */
    OP_FILE_EXISTS,  /* Check if file exists */
    OP_FILE_LINES,   /* Read file as array of lines */
    OP_FILE_WRITE_BYTES, /* Write byte array to file */

    /* HTTP */
    OP_HTTP_GET,     /* HTTP GET request */
    OP_HTTP_POST,    /* HTTP POST request */
    OP_HTTP_PUT,     /* HTTP PUT request */
    OP_HTTP_DELETE,  /* HTTP DELETE request */
    OP_HTTP_PATCH,   /* HTTP PATCH request */
    OP_HTTP_REQUEST, /* Generic HTTP request with headers */

    /* Shell */
    OP_SHELL,        /* Execute shell command */

    /* JSON */
    OP_JSON_PARSE,   /* Parse JSON string to value */
    OP_JSON_ENCODE,  /* Encode value to JSON string */

    /* Environment */
    OP_ENV_GET,      /* Get environment variable */
    OP_ENV_SET,      /* Set environment variable */

    /* Time */
    OP_SLEEP,        /* Sleep for milliseconds */
    OP_TIME,         /* Get current timestamp (ms) */
    OP_TIME_FORMAT,  /* Format timestamp to string */

    /* Random */
    OP_RANDOM,       /* Random float 0-1 */
    OP_RANDOM_INT,   /* Random int in range */

    /* String operations */
    OP_SPLIT,        /* Split string by delimiter */
    OP_JOIN,         /* Join array with delimiter */
    OP_TRIM,         /* Trim whitespace */
    OP_REPLACE,      /* Replace substring */
    OP_CONTAINS,     /* Check if string contains substring */
    OP_STARTS_WITH,  /* Check if string starts with prefix */
    OP_ENDS_WITH,    /* Check if string ends with suffix */
    OP_UPPER,        /* Convert to uppercase */
    OP_LOWER,        /* Convert to lowercase */
    OP_CHAR_AT,      /* Get character at index */
    OP_INDEX_OF,     /* Find index of substring */

    /* Base64 */
    OP_BASE64_ENCODE, /* Encode to base64 */
    OP_BASE64_DECODE, /* Decode from base64 */

    /* I/O */
    OP_READ_STDIN,   /* Read from stdin */
    OP_PRINT_ERR,    /* Print to stderr */

    /* Math */
    OP_FLOOR,        /* Floor of float */
    OP_CEIL,         /* Ceiling of float */
    OP_ROUND,        /* Round float */
    OP_ABS,          /* Absolute value */
    OP_SQRT,         /* Square root */
    OP_POW,          /* Power */
    OP_MIN,          /* Minimum of two values */
    OP_MAX,          /* Maximum of two values */

    /* WebSocket */
    OP_WS_CONNECT,   /* Connect to WebSocket */
    OP_WS_SEND,      /* Send message */
    OP_WS_RECV,      /* Receive message */
    OP_WS_CLOSE,     /* Close connection */

    /* Streaming */
    OP_HTTP_STREAM,  /* HTTP streaming (SSE) */
    OP_STREAM_READ,  /* Read from stream */
    OP_STREAM_CLOSE, /* Close stream */

    /* Process */
    OP_EXEC,         /* Execute command with stdin/stdout */
    OP_EXEC_ASYNC,   /* Execute async, return handle */
    OP_PROC_WRITE,   /* Write to process stdin */
    OP_PROC_READ,    /* Read from process stdout */
    OP_PROC_CLOSE,   /* Close process */

    /* UUID */
    OP_UUID,         /* Generate UUID v4 */

    /* Hashing */
    OP_HASH_MD5,     /* MD5 hash */
    OP_HASH_SHA256,  /* SHA256 hash */

    /* Debug */
    OP_PRINT,        /* Print value (debug) */

    /* Result operations (error handling) */
    OP_RESULT_OK,    /* Wrap top of stack in Ok() */
    OP_RESULT_ERR,   /* Wrap top of stack in Err() */
    OP_RESULT_IS_OK, /* Push bool: is result Ok? */
    OP_RESULT_IS_ERR,/* Push bool: is result Err? */
    OP_RESULT_UNWRAP,/* Unwrap result or runtime error */
    OP_RESULT_UNWRAP_OR, /* Unwrap result with default */
    OP_RESULT_MATCH, /* Branch based on Ok/Err: [ok_jump:16][err_jump:16] */

    /* Tool introspection */
    OP_LIST_TOOLS,   /* Push array of tool info */
    OP_TOOL_SCHEMA,  /* Push JSON schema string for tool (name on stack) */

    /* Option operations */
    OP_SOME,         /* Wrap top of stack in Some() */
    OP_NONE,         /* Push None */
    OP_IS_SOME,      /* Push bool: is option Some? */
    OP_IS_NONE,      /* Push bool: is option None? */
    OP_UNWRAP_OPTION,/* Unwrap option or runtime error */
    OP_UNWRAP_OPTION_OR, /* Unwrap option with default */

    /* Struct operations */
    OP_STRUCT_NEW,   /* Create struct: [type_name_idx:16][field_count:8] */
    OP_STRUCT_GET,   /* Get field: [field_name_idx:16] */
    OP_STRUCT_SET,   /* Set field: [field_name_idx:16] */
    OP_STRUCT_GET_INDEX, /* Get field by index: [index:8] */

    /* Enum operations */
    OP_ENUM_NEW,     /* Create enum: [type_idx:16][variant_idx:16][has_payload:8] */
    OP_ENUM_IS,      /* Check variant: [variant_idx:16] */
    OP_ENUM_PAYLOAD, /* Get enum payload */

    /* End */
    OP_HALT,         /* Stop execution */
} Opcode;

/*============================================================================
 * Bytecode Chunk
 *============================================================================*/

typedef struct Chunk {
    /* Bytecode */
    uint8_t *code;
    size_t code_size;
    size_t code_capacity;

    /* Constants pool */
    Value **constants;
    size_t constants_size;
    size_t constants_capacity;

    /* Inline cache slots */
    InlineCache *ic_slots;
    size_t ic_count;
    size_t ic_capacity;

    /* Line numbers for debugging */
    int *lines;
    size_t lines_capacity;
} Chunk;

/*============================================================================
 * Tool Metadata (for user-defined tools in bytecode)
 *============================================================================*/

typedef struct ToolParamMeta {
    char *name;
    char *type;         /* Optional type hint */
} ToolParamMeta;

typedef struct ToolInfo {
    char *name;
    char *description;  /* Optional docstring */
    ToolParamMeta *params;
    size_t param_count;
    char *return_type;  /* Optional return type hint */
    size_t func_index;  /* Index into functions array */
} ToolInfo;

/*============================================================================
 * Bytecode Container
 *============================================================================*/

typedef struct Bytecode {
    /* Reference count for sharing across agents */
    _Atomic(uint32_t) refcount;

    /* Main chunk */
    Chunk *main;

    /* Function chunks */
    Chunk **functions;
    size_t functions_count;
    size_t functions_capacity;

    /* String table */
    char **strings;
    size_t strings_count;
    size_t strings_capacity;

    /* User-defined tools */
    ToolInfo *tools;
    size_t tools_count;
    size_t tools_capacity;

    /* Metadata */
    char *source_name;
    uint32_t version;
} Bytecode;

/*============================================================================
 * Chunk API
 *============================================================================*/

Chunk *chunk_new(void);
void chunk_free(Chunk *chunk);

/* Write operations */
void chunk_write_byte(Chunk *chunk, uint8_t byte, int line);
void chunk_write_opcode(Chunk *chunk, Opcode op, int line);
size_t chunk_write_arg(Chunk *chunk, uint16_t arg, int line);
size_t chunk_write_jump(Chunk *chunk, Opcode op, int line);
void chunk_patch_jump(Chunk *chunk, size_t offset);

/* Constants */
size_t chunk_add_constant(Chunk *chunk, Value *value);

/* Inline cache */
size_t chunk_alloc_ic(Chunk *chunk);

/* Read operations */
uint8_t chunk_read_byte(Chunk *chunk, size_t offset);
uint16_t chunk_read_arg(Chunk *chunk, size_t offset);

/*============================================================================
 * Bytecode API
 *============================================================================*/

Bytecode *bytecode_new(void);
void bytecode_free(Bytecode *code);

/**
 * Increment bytecode reference count (for sharing).
 * Returns the bytecode for chaining.
 */
Bytecode *bytecode_retain(Bytecode *code);

/**
 * Decrement bytecode reference count and free if zero.
 */
void bytecode_release(Bytecode *code);

/* Functions */
size_t bytecode_add_function(Bytecode *code, Chunk *chunk);

/* Strings */
size_t bytecode_add_string(Bytecode *code, const char *str);
const char *bytecode_get_string(Bytecode *code, size_t index);

/* Tools */
size_t bytecode_add_tool(Bytecode *code, const char *name, size_t func_index,
                         const char **param_names, const char **param_types,
                         size_t param_count, const char *return_type,
                         const char *description);
const ToolInfo *bytecode_get_tools(Bytecode *code, size_t *count);
const ToolInfo *bytecode_find_tool(Bytecode *code, const char *name);

/* Serialization */
uint8_t *bytecode_serialize(const Bytecode *code, size_t *size);
Bytecode *bytecode_deserialize(const uint8_t *data, size_t size);

/* Debug */
void chunk_disassemble(Chunk *chunk, const char *name);
size_t chunk_disassemble_instruction(Chunk *chunk, size_t offset);

#endif /* AGIM_VM_BYTECODE_H */
