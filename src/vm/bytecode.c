/*
 * Agim - Bytecode Format
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#define _POSIX_C_SOURCE 200809L

#include "vm/bytecode.h"
#include "vm/ic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AGIM_MAGIC 0x4147494D
#define AGIM_BYTECODE_VERSION 1

/* Memory Helpers */

static void *alloc(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "agim: out of memory\n");
        exit(1);
    }
    return ptr;
}

static void *realloc_safe(void *ptr, size_t size) {
    void *new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        fprintf(stderr, "agim: out of memory\n");
        exit(1);
    }
    return new_ptr;
}

/* Chunk Implementation */

Chunk *chunk_new(void) {
    Chunk *chunk = alloc(sizeof(Chunk));

    chunk->code_capacity = 256;
    chunk->code_size = 0;
    chunk->code = alloc(chunk->code_capacity);

    chunk->constants_capacity = 64;
    chunk->constants_size = 0;
    chunk->constants = alloc(sizeof(Value *) * chunk->constants_capacity);

    chunk->ic_capacity = 16;
    chunk->ic_count = 0;
    chunk->ic_slots = alloc(sizeof(InlineCache) * chunk->ic_capacity);

    chunk->lines_capacity = 256;
    chunk->lines = alloc(sizeof(int) * chunk->lines_capacity);

    return chunk;
}

void chunk_free(Chunk *chunk) {
    if (!chunk) return;

    free(chunk->code);

    for (size_t i = 0; i < chunk->constants_size; i++) {
        value_free(chunk->constants[i]);
    }
    free(chunk->constants);

    free(chunk->ic_slots);

    free(chunk->lines);
    free(chunk);
}

void chunk_write_byte(Chunk *chunk, uint8_t byte, int line) {
    if (chunk->code_size >= chunk->code_capacity) {
        chunk->code_capacity *= 2;
        chunk->code = realloc_safe(chunk->code, chunk->code_capacity);
    }

    if (chunk->code_size >= chunk->lines_capacity) {
        chunk->lines_capacity *= 2;
        chunk->lines = realloc_safe(chunk->lines, sizeof(int) * chunk->lines_capacity);
    }

    chunk->code[chunk->code_size] = byte;
    chunk->lines[chunk->code_size] = line;
    chunk->code_size++;
}

void chunk_write_opcode(Chunk *chunk, Opcode op, int line) {
    chunk_write_byte(chunk, (uint8_t)op, line);
}

size_t chunk_write_arg(Chunk *chunk, uint16_t arg, int line) {
    size_t offset = chunk->code_size;
    chunk_write_byte(chunk, (arg >> 8) & 0xFF, line);
    chunk_write_byte(chunk, arg & 0xFF, line);
    return offset;
}

size_t chunk_write_jump(Chunk *chunk, Opcode op, int line) {
    chunk_write_opcode(chunk, op, line);
    size_t offset = chunk->code_size;
    chunk_write_byte(chunk, 0xFF, line);
    chunk_write_byte(chunk, 0xFF, line);
    return offset;
}

void chunk_patch_jump(Chunk *chunk, size_t offset) {
    size_t jump = chunk->code_size - offset - 2;

    if (jump > UINT16_MAX) {
        fprintf(stderr, "agim: jump too large\n");
        return;
    }

    chunk->code[offset] = (jump >> 8) & 0xFF;
    chunk->code[offset + 1] = jump & 0xFF;
}

size_t chunk_add_constant(Chunk *chunk, Value *value) {
    if (chunk->constants_size >= chunk->constants_capacity) {
        chunk->constants_capacity *= 2;
        chunk->constants = realloc_safe(
            chunk->constants,
            sizeof(Value *) * chunk->constants_capacity);
    }

    chunk->constants[chunk->constants_size] = value;
    return chunk->constants_size++;
}

size_t chunk_alloc_ic(Chunk *chunk) {
    if (chunk->ic_count >= chunk->ic_capacity) {
        chunk->ic_capacity *= 2;
        chunk->ic_slots = realloc_safe(
            chunk->ic_slots,
            sizeof(InlineCache) * chunk->ic_capacity);
    }

    size_t slot = chunk->ic_count++;
    ic_init(&chunk->ic_slots[slot]);
    return slot;
}

uint8_t chunk_read_byte(Chunk *chunk, size_t offset) {
    if (offset >= chunk->code_size) return 0;
    return chunk->code[offset];
}

uint16_t chunk_read_arg(Chunk *chunk, size_t offset) {
    return (uint16_t)(chunk->code[offset] << 8) | chunk->code[offset + 1];
}

/* Bytecode Implementation */

Bytecode *bytecode_new(void) {
    Bytecode *code = alloc(sizeof(Bytecode));

    atomic_store(&code->refcount, 1);
    code->main = chunk_new();

    code->functions_capacity = 16;
    code->functions_count = 0;
    code->functions = alloc(sizeof(Chunk *) * code->functions_capacity);

    code->strings_capacity = 64;
    code->strings_count = 0;
    code->strings = alloc(sizeof(char *) * code->strings_capacity);

    code->tools_capacity = 8;
    code->tools_count = 0;
    code->tools = alloc(sizeof(ToolInfo) * code->tools_capacity);

    code->source_name = NULL;
    code->version = AGIM_BYTECODE_VERSION;

    return code;
}

void bytecode_free(Bytecode *code) {
    if (!code) return;

    chunk_free(code->main);

    for (size_t i = 0; i < code->functions_count; i++) {
        chunk_free(code->functions[i]);
    }
    free(code->functions);

    for (size_t i = 0; i < code->strings_count; i++) {
        free(code->strings[i]);
    }
    free(code->strings);

    for (size_t i = 0; i < code->tools_count; i++) {
        free(code->tools[i].name);
        free(code->tools[i].description);
        free(code->tools[i].return_type);
        for (size_t j = 0; j < code->tools[i].param_count; j++) {
            free(code->tools[i].params[j].name);
            free(code->tools[i].params[j].type);
            free(code->tools[i].params[j].description);
        }
        free(code->tools[i].params);
    }
    free(code->tools);

    free(code->source_name);
    free(code);
}

Bytecode *bytecode_retain(Bytecode *code) {
    if (code) {
        atomic_fetch_add(&code->refcount, 1);
    }
    return code;
}

void bytecode_release(Bytecode *code) {
    if (!code) return;

    uint32_t prev = atomic_fetch_sub(&code->refcount, 1);
    if (prev == 1) {
        bytecode_free(code);
    }
}

size_t bytecode_add_function(Bytecode *code, Chunk *chunk) {
    if (code->functions_count >= code->functions_capacity) {
        code->functions_capacity *= 2;
        code->functions = realloc_safe(
            code->functions,
            sizeof(Chunk *) * code->functions_capacity);
    }

    code->functions[code->functions_count] = chunk;
    return code->functions_count++;
}

size_t bytecode_add_string(Bytecode *code, const char *str) {
    for (size_t i = 0; i < code->strings_count; i++) {
        if (strcmp(code->strings[i], str) == 0) {
            return i;
        }
    }

    if (code->strings_count >= code->strings_capacity) {
        code->strings_capacity *= 2;
        code->strings = realloc_safe(
            code->strings,
            sizeof(char *) * code->strings_capacity);
    }

    code->strings[code->strings_count] = strdup(str);
    return code->strings_count++;
}

const char *bytecode_get_string(Bytecode *code, size_t index) {
    if (index >= code->strings_count) return NULL;
    return code->strings[index];
}

/* Tools */

size_t bytecode_add_tool(Bytecode *code, const char *name, size_t func_index,
                         const char **param_names, const char **param_types,
                         const char **param_descriptions, size_t param_count,
                         const char *return_type, const char *description) {
    if (!code || !name) return (size_t)-1;

    if (code->tools_count >= code->tools_capacity) {
        code->tools_capacity *= 2;
        code->tools = realloc_safe(code->tools,
                                   sizeof(ToolInfo) * code->tools_capacity);
    }

    ToolInfo *tool = &code->tools[code->tools_count];
    tool->name = strdup(name);
    tool->description = description ? strdup(description) : NULL;
    tool->func_index = func_index;
    tool->return_type = return_type ? strdup(return_type) : NULL;

    tool->param_count = param_count;
    if (param_count > 0) {
        tool->params = alloc(sizeof(ToolParamMeta) * param_count);
        for (size_t i = 0; i < param_count; i++) {
            tool->params[i].name = param_names[i] ? strdup(param_names[i]) : NULL;
            tool->params[i].type = param_types && param_types[i] ? strdup(param_types[i]) : NULL;
            tool->params[i].description = param_descriptions && param_descriptions[i]
                ? strdup(param_descriptions[i]) : NULL;
        }
    } else {
        tool->params = NULL;
    }

    return code->tools_count++;
}

const ToolInfo *bytecode_get_tools(Bytecode *code, size_t *count) {
    if (!code) {
        if (count) *count = 0;
        return NULL;
    }
    if (count) *count = code->tools_count;
    return code->tools;
}

const ToolInfo *bytecode_find_tool(Bytecode *code, const char *name) {
    if (!code || !name) return NULL;

    for (size_t i = 0; i < code->tools_count; i++) {
        if (strcmp(code->tools[i].name, name) == 0) {
            return &code->tools[i];
        }
    }
    return NULL;
}

/* Serialization */

static void write_u32(uint8_t **buf, uint32_t val) {
    (*buf)[0] = (val >> 24) & 0xFF;
    (*buf)[1] = (val >> 16) & 0xFF;
    (*buf)[2] = (val >> 8) & 0xFF;
    (*buf)[3] = val & 0xFF;
    *buf += 4;
}

static size_t serialize_value(uint8_t *buf, Value *val) {
    size_t offset = 0;
    buf[offset++] = (uint8_t)val->type;

    switch (val->type) {
    case VAL_NIL:
        break;
    case VAL_BOOL:
        buf[offset++] = val->as.boolean ? 1 : 0;
        break;
    case VAL_INT: {
        uint64_t v = (uint64_t)val->as.integer;
        for (int i = 7; i >= 0; i--) {
            buf[offset++] = (v >> (i * 8)) & 0xFF;
        }
        break;
    }
    case VAL_FLOAT: {
        union { double d; uint64_t u; } conv;
        conv.d = val->as.floating;
        for (int i = 7; i >= 0; i--) {
            buf[offset++] = (conv.u >> (i * 8)) & 0xFF;
        }
        break;
    }
    case VAL_STRING: {
        size_t len = strlen(val->as.string->data);
        buf[offset++] = (len >> 24) & 0xFF;
        buf[offset++] = (len >> 16) & 0xFF;
        buf[offset++] = (len >> 8) & 0xFF;
        buf[offset++] = len & 0xFF;
        memcpy(buf + offset, val->as.string->data, len);
        offset += len;
        break;
    }
    default:
        break;
    }
    return offset;
}

static size_t serialize_chunk(uint8_t *buf, Chunk *chunk) {
    uint8_t *p = buf;

    write_u32(&p, (uint32_t)chunk->code_size);
    memcpy(p, chunk->code, chunk->code_size);
    p += chunk->code_size;

    for (size_t i = 0; i < chunk->code_size; i++) {
        write_u32(&p, (uint32_t)chunk->lines[i]);
    }

    write_u32(&p, (uint32_t)chunk->constants_size);

    for (size_t i = 0; i < chunk->constants_size; i++) {
        p += serialize_value(p, chunk->constants[i]);
    }

    return (size_t)(p - buf);
}

uint8_t *bytecode_serialize(const Bytecode *code, size_t *size) {
    size_t total = 8;

    total += 4 + code->main->code_size;
    total += code->main->code_size * 4;
    total += 4 + code->main->constants_size * 64;

    total += 4;
    for (size_t i = 0; i < code->functions_count; i++) {
        total += 4 + code->functions[i]->code_size;
        total += code->functions[i]->code_size * 4;
        total += 4 + code->functions[i]->constants_size * 64;
    }

    total += 4;
    for (size_t i = 0; i < code->strings_count; i++) {
        total += 4 + strlen(code->strings[i]) + 1;
    }

    uint8_t *buffer = alloc(total);
    uint8_t *p = buffer;

    write_u32(&p, AGIM_MAGIC);
    write_u32(&p, code->version);

    p += serialize_chunk(p, code->main);

    write_u32(&p, (uint32_t)code->functions_count);

    for (size_t i = 0; i < code->functions_count; i++) {
        p += serialize_chunk(p, code->functions[i]);
    }

    write_u32(&p, (uint32_t)code->strings_count);

    for (size_t i = 0; i < code->strings_count; i++) {
        size_t len = strlen(code->strings[i]);
        write_u32(&p, (uint32_t)len);
        memcpy(p, code->strings[i], len);
        p += len;
    }

    *size = (size_t)(p - buffer);
    return buffer;
}

static uint32_t read_u32(const uint8_t **buf) {
    uint32_t val = ((*buf)[0] << 24) | ((*buf)[1] << 16) |
                   ((*buf)[2] << 8) | (*buf)[3];
    *buf += 4;
    return val;
}

static uint64_t read_u64(const uint8_t **buf) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val = (val << 8) | (*buf)[i];
    }
    *buf += 8;
    return val;
}

static Value *deserialize_value(const uint8_t **buf, const uint8_t *end) {
    /* Need at least 1 byte for type */
    if (*buf + 1 > end) return NULL;

    uint8_t type = (*buf)[0];
    (*buf)++;

    switch ((ValueType)type) {
    case VAL_NIL:
        return value_nil();
    case VAL_BOOL:
        if (*buf + 1 > end) return NULL;
        return value_bool((*buf)++[0] != 0);
    case VAL_INT: {
        if (*buf + 8 > end) return NULL;
        int64_t val = (int64_t)read_u64(buf);
        return value_int(val);
    }
    case VAL_FLOAT: {
        if (*buf + 8 > end) return NULL;
        union { double d; uint64_t u; } conv;
        conv.u = read_u64(buf);
        return value_float(conv.d);
    }
    case VAL_STRING: {
        if (*buf + 4 > end) return NULL;
        uint32_t len = read_u32(buf);
        if (*buf + len > end) return NULL;
        char *str = malloc(len + 1);
        if (!str) return NULL;
        memcpy(str, *buf, len);
        str[len] = '\0';
        *buf += len;
        Value *v = value_string(str);
        free(str);
        return v;
    }
    default:
        return NULL;
    }
}

static bool deserialize_chunk(const uint8_t **buf, const uint8_t *end, Chunk *chunk) {
    if (*buf + 4 > end) return false;

    uint32_t code_size = read_u32(buf);

    /* Bounds check for code data */
    if (code_size > (size_t)(end - *buf)) return false;

    /* Prevent excessive allocation (max 16MB of bytecode) */
    if (code_size > 16 * 1024 * 1024) return false;

    while (chunk->code_capacity < code_size) {
        chunk->code_capacity *= 2;
        chunk->code = realloc_safe(chunk->code, chunk->code_capacity);
    }
    memcpy(chunk->code, *buf, code_size);
    chunk->code_size = code_size;
    *buf += code_size;

    /* code_size is already bounded to 16MB above, so no overflow possible */
    size_t lines_bytes = (size_t)code_size * 4;
    if (lines_bytes > (size_t)(end - *buf)) return false;

    while (chunk->lines_capacity < code_size) {
        chunk->lines_capacity *= 2;
        chunk->lines = realloc_safe(chunk->lines, sizeof(int) * chunk->lines_capacity);
    }
    for (size_t i = 0; i < code_size; i++) {
        chunk->lines[i] = (int)read_u32(buf);
    }

    if (*buf + 4 > end) return false;
    uint32_t const_count = read_u32(buf);

    /* Prevent excessive constant count (max 1M constants) */
    if (const_count > 1024 * 1024) return false;

    for (uint32_t i = 0; i < const_count; i++) {
        Value *val = deserialize_value(buf, end);
        if (!val) return false;
        chunk_add_constant(chunk, val);
    }

    return true;
}

Bytecode *bytecode_deserialize(const uint8_t *data, size_t size) {
    if (!data || size < 8) return NULL;

    const uint8_t *p = data;
    const uint8_t *end = data + size;

    uint32_t magic = read_u32(&p);
    if (magic != AGIM_MAGIC) {
        fprintf(stderr, "agim: invalid bytecode file\n");
        return NULL;
    }

    uint32_t version = read_u32(&p);
    if (version > AGIM_BYTECODE_VERSION) {
        fprintf(stderr, "agim: bytecode version %u not supported\n", version);
        return NULL;
    }

    Bytecode *code = bytecode_new();
    code->version = version;

    if (!deserialize_chunk(&p, end, code->main)) goto error;

    if (p + 4 > end) goto error;
    uint32_t func_count = read_u32(&p);

    for (uint32_t i = 0; i < func_count; i++) {
        Chunk *chunk = chunk_new();
        if (!deserialize_chunk(&p, end, chunk)) {
            chunk_free(chunk);
            goto error;
        }
        bytecode_add_function(code, chunk);
    }

    if (p + 4 > end) goto error;
    uint32_t str_count = read_u32(&p);

    for (uint32_t i = 0; i < str_count; i++) {
        if (p + 4 > end) goto error;
        uint32_t len = read_u32(&p);
        if (p + len > end) goto error;
        char *str = malloc(len + 1);
        if (!str) goto error;
        memcpy(str, p, len);
        str[len] = '\0';
        p += len;
        bytecode_add_string(code, str);
        free(str);
    }

    return code;

error:
    bytecode_free(code);
    return NULL;
}

/* Disassembly */

static const char *opcode_names[] = {
    [OP_NOP] = "NOP",
    [OP_POP] = "POP",
    [OP_DUP] = "DUP",
    [OP_DUP2] = "DUP2",
    [OP_SWAP] = "SWAP",
    [OP_CONST] = "CONST",
    [OP_NIL] = "NIL",
    [OP_TRUE] = "TRUE",
    [OP_FALSE] = "FALSE",
    [OP_ADD] = "ADD",
    [OP_SUB] = "SUB",
    [OP_MUL] = "MUL",
    [OP_DIV] = "DIV",
    [OP_MOD] = "MOD",
    [OP_NEG] = "NEG",
    [OP_EQ] = "EQ",
    [OP_NE] = "NE",
    [OP_LT] = "LT",
    [OP_LE] = "LE",
    [OP_GT] = "GT",
    [OP_GE] = "GE",
    [OP_NOT] = "NOT",
    [OP_AND] = "AND",
    [OP_OR] = "OR",
    [OP_GET_LOCAL] = "GET_LOCAL",
    [OP_SET_LOCAL] = "SET_LOCAL",
    [OP_GET_GLOBAL] = "GET_GLOBAL",
    [OP_SET_GLOBAL] = "SET_GLOBAL",
    [OP_JUMP] = "JUMP",
    [OP_JUMP_IF] = "JUMP_IF",
    [OP_JUMP_UNLESS] = "JUMP_UNLESS",
    [OP_LOOP] = "LOOP",
    [OP_CALL] = "CALL",
    [OP_RETURN] = "RETURN",
    [OP_CLOSURE] = "CLOSURE",
    [OP_ARRAY_NEW] = "ARRAY_NEW",
    [OP_ARRAY_PUSH] = "ARRAY_PUSH",
    [OP_ARRAY_GET] = "ARRAY_GET",
    [OP_ARRAY_SET] = "ARRAY_SET",
    [OP_MAP_NEW] = "MAP_NEW",
    [OP_MAP_GET] = "MAP_GET",
    [OP_MAP_SET] = "MAP_SET",
    [OP_MAP_GET_IC] = "MAP_GET_IC",
    [OP_CONCAT] = "CONCAT",
    [OP_SPAWN] = "SPAWN",
    [OP_SEND] = "SEND",
    [OP_RECEIVE] = "RECEIVE",
    [OP_SELF] = "SELF",
    [OP_YIELD] = "YIELD",
    [OP_INFER] = "INFER",
    [OP_TOOL_CALL] = "TOOL_CALL",
    [OP_MEMORY_GET] = "MEMORY_GET",
    [OP_MEMORY_SET] = "MEMORY_SET",
    [OP_LEN] = "LEN",
    [OP_TYPE] = "TYPE",
    [OP_KEYS] = "KEYS",
    [OP_PUSH] = "PUSH",
    [OP_POP_ARRAY] = "POP_ARRAY",
    [OP_SLICE] = "SLICE",
    [OP_TO_STRING] = "TO_STRING",
    [OP_TO_INT] = "TO_INT",
    [OP_TO_FLOAT] = "TO_FLOAT",
    [OP_FILE_READ] = "FILE_READ",
    [OP_FILE_WRITE] = "FILE_WRITE",
    [OP_FILE_EXISTS] = "FILE_EXISTS",
    [OP_FILE_LINES] = "FILE_LINES",
    [OP_HTTP_GET] = "HTTP_GET",
    [OP_HTTP_POST] = "HTTP_POST",
    [OP_HTTP_PUT] = "HTTP_PUT",
    [OP_HTTP_DELETE] = "HTTP_DELETE",
    [OP_HTTP_PATCH] = "HTTP_PATCH",
    [OP_HTTP_REQUEST] = "HTTP_REQUEST",
    [OP_SHELL] = "SHELL",
    [OP_JSON_PARSE] = "JSON_PARSE",
    [OP_JSON_ENCODE] = "JSON_ENCODE",
    [OP_ENV_GET] = "ENV_GET",
    [OP_ENV_SET] = "ENV_SET",
    [OP_SLEEP] = "SLEEP",
    [OP_TIME] = "TIME",
    [OP_TIME_FORMAT] = "TIME_FORMAT",
    [OP_RANDOM] = "RANDOM",
    [OP_RANDOM_INT] = "RANDOM_INT",
    [OP_SPLIT] = "SPLIT",
    [OP_JOIN] = "JOIN",
    [OP_TRIM] = "TRIM",
    [OP_REPLACE] = "REPLACE",
    [OP_CONTAINS] = "CONTAINS",
    [OP_STARTS_WITH] = "STARTS_WITH",
    [OP_ENDS_WITH] = "ENDS_WITH",
    [OP_UPPER] = "UPPER",
    [OP_LOWER] = "LOWER",
    [OP_CHAR_AT] = "CHAR_AT",
    [OP_INDEX_OF] = "INDEX_OF",
    [OP_BASE64_ENCODE] = "BASE64_ENCODE",
    [OP_BASE64_DECODE] = "BASE64_DECODE",
    [OP_READ_STDIN] = "READ_STDIN",
    [OP_PRINT_ERR] = "PRINT_ERR",
    [OP_FLOOR] = "FLOOR",
    [OP_CEIL] = "CEIL",
    [OP_ROUND] = "ROUND",
    [OP_ABS] = "ABS",
    [OP_SQRT] = "SQRT",
    [OP_POW] = "POW",
    [OP_MIN] = "MIN",
    [OP_MAX] = "MAX",
    [OP_WS_CONNECT] = "WS_CONNECT",
    [OP_WS_SEND] = "WS_SEND",
    [OP_WS_RECV] = "WS_RECV",
    [OP_WS_CLOSE] = "WS_CLOSE",
    [OP_HTTP_STREAM] = "HTTP_STREAM",
    [OP_STREAM_READ] = "STREAM_READ",
    [OP_STREAM_CLOSE] = "STREAM_CLOSE",
    [OP_EXEC] = "EXEC",
    [OP_EXEC_ASYNC] = "EXEC_ASYNC",
    [OP_PROC_WRITE] = "PROC_WRITE",
    [OP_PROC_READ] = "PROC_READ",
    [OP_PROC_CLOSE] = "PROC_CLOSE",
    [OP_UUID] = "UUID",
    [OP_HASH_MD5] = "HASH_MD5",
    [OP_HASH_SHA256] = "HASH_SHA256",
    [OP_PRINT] = "PRINT",
    [OP_RESULT_OK] = "RESULT_OK",
    [OP_RESULT_ERR] = "RESULT_ERR",
    [OP_RESULT_IS_OK] = "RESULT_IS_OK",
    [OP_RESULT_IS_ERR] = "RESULT_IS_ERR",
    [OP_RESULT_UNWRAP] = "RESULT_UNWRAP",
    [OP_RESULT_UNWRAP_OR] = "RESULT_UNWRAP_OR",
    [OP_RESULT_MATCH] = "RESULT_MATCH",
    [OP_LIST_TOOLS] = "LIST_TOOLS",
    [OP_TOOL_SCHEMA] = "TOOL_SCHEMA",
    [OP_SOME] = "SOME",
    [OP_NONE] = "NONE",
    [OP_IS_SOME] = "IS_SOME",
    [OP_IS_NONE] = "IS_NONE",
    [OP_UNWRAP_OPTION] = "UNWRAP_OPTION",
    [OP_UNWRAP_OPTION_OR] = "UNWRAP_OPTION_OR",
    [OP_STRUCT_NEW] = "STRUCT_NEW",
    [OP_STRUCT_GET] = "STRUCT_GET",
    [OP_STRUCT_SET] = "STRUCT_SET",
    [OP_STRUCT_GET_INDEX] = "STRUCT_GET_INDEX",
    [OP_ENUM_NEW] = "ENUM_NEW",
    [OP_ENUM_IS] = "ENUM_IS",
    [OP_ENUM_PAYLOAD] = "ENUM_PAYLOAD",
    [OP_HALT] = "HALT",
};

void chunk_disassemble(Chunk *chunk, const char *name) {
    printf("== %s ==\n", name);

    size_t offset = 0;
    while (offset < chunk->code_size) {
        offset = chunk_disassemble_instruction(chunk, offset);
    }
}

size_t chunk_disassemble_instruction(Chunk *chunk, size_t offset) {
    printf("%04zu ", offset);

    if (offset > 0 && chunk->lines[offset] == chunk->lines[offset - 1]) {
        printf("   | ");
    } else {
        printf("%4d ", chunk->lines[offset]);
    }

    uint8_t instruction = chunk->code[offset];

    if (instruction < sizeof(opcode_names) / sizeof(opcode_names[0]) &&
        opcode_names[instruction]) {
        printf("%s", opcode_names[instruction]);
    } else {
        printf("UNKNOWN(%d)", instruction);
    }

    switch (instruction) {
    case OP_CONST:
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_CALL:
    case OP_CLOSURE: {
        uint16_t arg = chunk_read_arg(chunk, offset + 1);
        printf(" %d", arg);
        if (instruction == OP_CONST && arg < chunk->constants_size) {
            printf(" (");
            value_print(chunk->constants[arg]);
            printf(")");
        }
        printf("\n");
        return offset + 3;
    }

    case OP_JUMP:
    case OP_JUMP_IF:
    case OP_JUMP_UNLESS: {
        uint16_t jump = chunk_read_arg(chunk, offset + 1);
        printf(" -> %zu\n", offset + 3 + jump);
        return offset + 3;
    }

    case OP_LOOP: {
        uint16_t jump = chunk_read_arg(chunk, offset + 1);
        printf(" -> %zu\n", offset + 3 - jump);
        return offset + 3;
    }

    case OP_MAP_GET_IC: {
        uint16_t key_idx = chunk_read_arg(chunk, offset + 1);
        uint16_t ic_slot = chunk_read_arg(chunk, offset + 3);
        printf(" key=%d ic=%d\n", key_idx, ic_slot);
        return offset + 5;
    }

    case OP_STRUCT_GET:
    case OP_STRUCT_SET:
    case OP_ENUM_IS: {
        uint16_t name_idx = chunk_read_arg(chunk, offset + 1);
        printf(" name=%d\n", name_idx);
        return offset + 3;
    }

    case OP_STRUCT_GET_INDEX: {
        uint8_t index = chunk->code[offset + 1];
        printf(" index=%d\n", index);
        return offset + 2;
    }

    case OP_STRUCT_NEW: {
        uint16_t type_idx = chunk_read_arg(chunk, offset + 1);
        uint8_t field_count = chunk->code[offset + 3];
        printf(" type=%d fields=%d\n", type_idx, field_count);
        return offset + 4 + (field_count * 2);
    }

    case OP_ENUM_NEW: {
        uint16_t type_idx = chunk_read_arg(chunk, offset + 1);
        uint16_t variant_idx = chunk_read_arg(chunk, offset + 3);
        uint8_t has_payload = chunk->code[offset + 5];
        printf(" type=%d variant=%d payload=%d\n", type_idx, variant_idx, has_payload);
        return offset + 6;
    }

    default:
        printf("\n");
        return offset + 1;
    }
}
