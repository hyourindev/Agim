/*
 * Agim - Compiler Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/compiler.h"
#include "lang/module.h"
#include "util/alloc.h"
#include "vm/value.h"
#include "debug/log.h"

#include <stdio.h>
#include <string.h>

/* Local Variable */

typedef struct {
    char *name;
    int depth;
    bool is_const;
} Local;

/* Loop Context */

typedef struct {
    size_t start;
    size_t *breaks;
    size_t break_count;
    size_t break_capacity;
    int scope_depth;
} LoopContext;

/* Function Context */

typedef struct FunctionContext {
    Chunk *chunk;
    Local locals[256];
    size_t local_count;
    int scope_depth;
    LoopContext loops[32];
    size_t loop_depth;
    struct FunctionContext *enclosing;
} FunctionContext;

/* Compiler Structure */

struct Compiler {
    Bytecode *code;
    FunctionContext *current;
    char *error;
    int error_line;
    bool had_error;
    ModuleCache *module_cache;  /* Cache for imported modules */
    char *source_path;          /* Path of current source file */
};

/* Error Handling */

static void compile_error(Compiler *c, int line, const char *message) {
    if (c->had_error) return;
    c->had_error = true;

    LOG_ERROR("compiler: line %d: %s", line, message ? message : "(no message)");

    /* Calculate required buffer size to avoid overflow */
    size_t msg_len = message ? strlen(message) : 0;
    size_t needed = 20 + msg_len; /* "line XXXXXXXXXX: " + message + null */
    c->error = agim_alloc(needed);
    if (c->error) {
        snprintf(c->error, needed, "line %d: %s", line, message ? message : "");
    }
    c->error_line = line;
}

/* Chunk Helpers */

static Chunk *current_chunk(Compiler *c) {
    return c->current->chunk;
}

static void emit_byte(Compiler *c, uint8_t byte, int line) {
    chunk_write_byte(current_chunk(c), byte, line);
}

static void emit_op(Compiler *c, Opcode op, int line) {
    chunk_write_opcode(current_chunk(c), op, line);
}

static void emit_bytes(Compiler *c, uint8_t b1, uint8_t b2, int line) {
    emit_byte(c, b1, line);
    emit_byte(c, b2, line);
}

static size_t emit_jump(Compiler *c, Opcode op, int line) {
    emit_op(c, op, line);
    emit_byte(c, 0xFF, line);
    emit_byte(c, 0xFF, line);
    return current_chunk(c)->code_size - 2;
}

static void patch_jump(Compiler *c, size_t offset) {
    chunk_patch_jump(current_chunk(c), offset);
}

static void emit_loop(Compiler *c, size_t loop_start, int line) {
    emit_op(c, OP_LOOP, line);

    size_t offset = current_chunk(c)->code_size - loop_start + 2;
    if (offset > 0xFFFF) {
        compile_error(c, line, "loop body too large");
        return;
    }

    emit_byte(c, (offset >> 8) & 0xFF, line);
    emit_byte(c, offset & 0xFF, line);
}

static size_t emit_constant(Compiler *c, Value *value, int line) {
    size_t index = chunk_add_constant(current_chunk(c), value);
    if (index > 0xFFFF) {
        compile_error(c, line, "too many constants");
        return 0;
    }

    emit_op(c, OP_CONST, line);
    emit_byte(c, (index >> 8) & 0xFF, line);
    emit_byte(c, index & 0xFF, line);
    return index;
}

/* Scope Management */

static void begin_scope(Compiler *c) {
    c->current->scope_depth++;
}

static void end_scope(Compiler *c, int line) {
    c->current->scope_depth--;

    /* Pop locals that are going out of scope */
    while (c->current->local_count > 0 &&
           c->current->locals[c->current->local_count - 1].depth > c->current->scope_depth) {
        emit_op(c, OP_POP, line);
        agim_free(c->current->locals[c->current->local_count - 1].name);
        c->current->local_count--;
    }
}

static void add_local(Compiler *c, const char *name, size_t length, bool is_const, int line) {
    if (c->current->local_count >= 256) {
        compile_error(c, line, "too many local variables");
        return;
    }

    /* Check for duplicate in current scope */
    for (int i = (int)c->current->local_count - 1; i >= 0; i--) {
        Local *local = &c->current->locals[i];
        if (local->depth < c->current->scope_depth) break;
        if (strlen(local->name) == length &&
            memcmp(local->name, name, length) == 0) {
            compile_error(c, line, "variable already declared in this scope");
            return;
        }
    }

    Local *local = &c->current->locals[c->current->local_count++];
    local->name = agim_alloc(length + 1);
    memcpy(local->name, name, length);
    local->name[length] = '\0';
    local->depth = c->current->scope_depth;
    local->is_const = is_const;
}

static int resolve_local(Compiler *c, const char *name, size_t length) {
    for (int i = (int)c->current->local_count - 1; i >= 0; i--) {
        Local *local = &c->current->locals[i];
        if (strlen(local->name) == length &&
            memcmp(local->name, name, length) == 0) {
            return i;
        }
    }
    return -1;
}

/* Loop Management */

static void begin_loop(Compiler *c, size_t start) {
    if (c->current->loop_depth >= 32) {
        compile_error(c, 0, "too many nested loops");
        return;
    }

    LoopContext *loop = &c->current->loops[c->current->loop_depth++];
    loop->start = start;
    loop->breaks = NULL;
    loop->break_count = 0;
    loop->break_capacity = 0;
    loop->scope_depth = c->current->scope_depth;
}

static void end_loop(Compiler *c) {
    if (c->current->loop_depth == 0) return;

    LoopContext *loop = &c->current->loops[--c->current->loop_depth];

    /* Patch all break jumps */
    for (size_t i = 0; i < loop->break_count; i++) {
        patch_jump(c, loop->breaks[i]);
    }

    if (loop->breaks) {
        agim_free(loop->breaks);
    }
}

static void emit_break(Compiler *c, int line) {
    if (c->current->loop_depth == 0) {
        compile_error(c, line, "break outside of loop");
        return;
    }

    LoopContext *loop = &c->current->loops[c->current->loop_depth - 1];

    /* Pop locals between here and loop scope */
    for (size_t i = c->current->local_count; i > 0; i--) {
        if (c->current->locals[i - 1].depth <= loop->scope_depth) break;
        emit_op(c, OP_POP, line);
    }

    /* Emit jump to be patched later */
    size_t jump = emit_jump(c, OP_JUMP, line);

    if (loop->break_count >= loop->break_capacity) {
        loop->break_capacity = loop->break_capacity == 0 ? 8 : loop->break_capacity * 2;
        loop->breaks = agim_realloc(loop->breaks, sizeof(size_t) * loop->break_capacity);
    }
    loop->breaks[loop->break_count++] = jump;
}

static void emit_continue(Compiler *c, int line) {
    if (c->current->loop_depth == 0) {
        compile_error(c, line, "continue outside of loop");
        return;
    }

    LoopContext *loop = &c->current->loops[c->current->loop_depth - 1];

    /* Pop locals between here and loop scope */
    for (size_t i = c->current->local_count; i > 0; i--) {
        if (c->current->locals[i - 1].depth <= loop->scope_depth) break;
        emit_op(c, OP_POP, line);
    }

    emit_loop(c, loop->start, line);
}

/* Forward Declarations */

static void compile_expr(Compiler *c, AstNode *node);
static void compile_stmt(Compiler *c, AstNode *node);
static void compile_decl(Compiler *c, AstNode *node);
static void compile_return(Compiler *c, AstNode *node);
static void compile_block_expr(Compiler *c, AstNode *node);

/* Expression Compilation */

static void compile_literal(Compiler *c, AstNode *node) {
    switch (node->type) {
    case NODE_NIL:
        emit_op(c, OP_NIL, node->line);
        break;
    case NODE_BOOL:
        emit_op(c, node->as.bool_val ? OP_TRUE : OP_FALSE, node->line);
        break;
    case NODE_INT:
        emit_constant(c, value_int(node->as.int_val), node->line);
        break;
    case NODE_FLOAT:
        emit_constant(c, value_float(node->as.float_val), node->line);
        break;
    case NODE_STRING:
        emit_constant(c, value_string(node->as.string_val), node->line);
        break;
    default:
        break;
    }
}

static void compile_ident(Compiler *c, AstNode *node) {
    const char *name = node->as.ident.name;
    size_t length = strlen(name);

    int slot = resolve_local(c, name, length);
    if (slot >= 0) {
        emit_op(c, OP_GET_LOCAL, node->line);
        emit_byte(c, (slot >> 8) & 0xFF, node->line);
        emit_byte(c, slot & 0xFF, node->line);
    } else {
        /* Global variable */
        size_t index = bytecode_add_string(c->code, name);
        emit_op(c, OP_GET_GLOBAL, node->line);
        emit_byte(c, (index >> 8) & 0xFF, node->line);
        emit_byte(c, index & 0xFF, node->line);
    }
}

static void compile_binary(Compiler *c, AstNode *node) {
    TokenType op = node->as.binary.op;

    /* Short-circuit for and/or */
    if (op == TOK_AND) {
        compile_expr(c, node->as.binary.left);
        size_t end_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
        emit_op(c, OP_POP, node->line);
        compile_expr(c, node->as.binary.right);
        patch_jump(c, end_jump);
        return;
    }

    if (op == TOK_OR) {
        compile_expr(c, node->as.binary.left);
        size_t else_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
        size_t end_jump = emit_jump(c, OP_JUMP, node->line);
        patch_jump(c, else_jump);
        emit_op(c, OP_POP, node->line);
        compile_expr(c, node->as.binary.right);
        patch_jump(c, end_jump);
        return;
    }

    /* Regular binary ops */
    compile_expr(c, node->as.binary.left);
    compile_expr(c, node->as.binary.right);

    switch (op) {
    case TOK_PLUS: emit_op(c, OP_ADD, node->line); break;
    case TOK_MINUS: emit_op(c, OP_SUB, node->line); break;
    case TOK_STAR: emit_op(c, OP_MUL, node->line); break;
    case TOK_SLASH: emit_op(c, OP_DIV, node->line); break;
    case TOK_PERCENT: emit_op(c, OP_MOD, node->line); break;
    case TOK_EQ: emit_op(c, OP_EQ, node->line); break;
    case TOK_NE: emit_op(c, OP_NE, node->line); break;
    case TOK_LT: emit_op(c, OP_LT, node->line); break;
    case TOK_LE: emit_op(c, OP_LE, node->line); break;
    case TOK_GT: emit_op(c, OP_GT, node->line); break;
    case TOK_GE: emit_op(c, OP_GE, node->line); break;
    default:
        compile_error(c, node->line, "unknown binary operator");
        break;
    }
}

static void compile_unary(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.unary.operand);

    switch (node->as.unary.op) {
    case TOK_MINUS: emit_op(c, OP_NEG, node->line); break;
    case TOK_NOT: emit_op(c, OP_NOT, node->line); break;
    default:
        compile_error(c, node->line, "unknown unary operator");
        break;
    }
}

static void compile_call(Compiler *c, AstNode *node) {
    /* Check for builtin functions */
    AstNode *callee = node->as.call.callee;

    /* Check for module-style calls like http.get(), ws.connect(), fs.read() */
    if (callee->type == NODE_MEMBER) {
        AstNode *obj = callee->as.member.object;
        const char *method = callee->as.member.field;

        /* http module: http.get, http.post, http.put, http.delete, http.patch, http.request */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "http") == 0) {
            if (strcmp(method, "get") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "http.get() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_HTTP_GET, node->line);
                return;
            }
            if (strcmp(method, "post") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "http.post() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_HTTP_POST, node->line);
                return;
            }
            if (strcmp(method, "put") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "http.put() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_HTTP_PUT, node->line);
                return;
            }
            if (strcmp(method, "delete") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "http.delete() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_HTTP_DELETE, node->line);
                return;
            }
            if (strcmp(method, "patch") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "http.patch() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_HTTP_PATCH, node->line);
                return;
            }
            if (strcmp(method, "request") == 0) {
                if (node->as.call.arg_count != 4) {
                    compile_error(c, node->line, "http.request() takes 4 arguments: method, url, body, headers");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                compile_expr(c, node->as.call.args[2]);
                compile_expr(c, node->as.call.args[3]);
                emit_op(c, OP_HTTP_REQUEST, node->line);
                return;
            }
            if (strcmp(method, "stream") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "http.stream() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_HTTP_STREAM, node->line);
                return;
            }
            compile_error(c, node->line, "unknown http method");
            return;
        }

        /* ws module: ws.connect, ws.send, ws.recv, ws.close */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "ws") == 0) {
            if (strcmp(method, "connect") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "ws.connect() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_WS_CONNECT, node->line);
                return;
            }
            if (strcmp(method, "send") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "ws.send() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_WS_SEND, node->line);
                return;
            }
            if (strcmp(method, "recv") == 0) {
                if (node->as.call.arg_count < 1 || node->as.call.arg_count > 2) {
                    compile_error(c, node->line, "ws.recv() takes 1 or 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                if (node->as.call.arg_count == 2) {
                    compile_expr(c, node->as.call.args[1]);
                } else {
                    /* Default: block forever (-1) */
                    emit_constant(c, value_int(-1), node->line);
                }
                emit_op(c, OP_WS_RECV, node->line);
                return;
            }
            if (strcmp(method, "close") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "ws.close() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_WS_CLOSE, node->line);
                return;
            }
            compile_error(c, node->line, "unknown ws method");
            return;
        }

        /* fs module: fs.read, fs.write, fs.exists, fs.lines */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "fs") == 0) {
            if (strcmp(method, "read") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "fs.read() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_FILE_READ, node->line);
                return;
            }
            if (strcmp(method, "write") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "fs.write() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_FILE_WRITE, node->line);
                return;
            }
            if (strcmp(method, "exists") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "fs.exists() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_FILE_EXISTS, node->line);
                return;
            }
            if (strcmp(method, "lines") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "fs.lines() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_FILE_LINES, node->line);
                return;
            }
            if (strcmp(method, "write_bytes") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "fs.write_bytes() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);  /* path */
                compile_expr(c, node->as.call.args[1]);  /* byte array */
                emit_op(c, OP_FILE_WRITE_BYTES, node->line);
                return;
            }
            compile_error(c, node->line, "unknown fs method");
            return;
        }

        /* json module: json.parse, json.encode */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "json") == 0) {
            if (strcmp(method, "parse") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "json.parse() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_JSON_PARSE, node->line);
                return;
            }
            if (strcmp(method, "encode") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "json.encode() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_JSON_ENCODE, node->line);
                return;
            }
            compile_error(c, node->line, "unknown json method");
            return;
        }

        /* env module: env.get, env.set */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "env") == 0) {
            if (strcmp(method, "get") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "env.get() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_ENV_GET, node->line);
                return;
            }
            if (strcmp(method, "set") == 0) {
                if (node->as.call.arg_count != 2) {
                    compile_error(c, node->line, "env.set() takes exactly 2 arguments");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                compile_expr(c, node->as.call.args[1]);
                emit_op(c, OP_ENV_SET, node->line);
                return;
            }
            compile_error(c, node->line, "unknown env method");
            return;
        }

        /* stream module: stream.read, stream.close */
        if (obj->type == NODE_IDENT && strcmp(obj->as.ident.name, "stream") == 0) {
            if (strcmp(method, "read") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "stream.read() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_STREAM_READ, node->line);
                return;
            }
            if (strcmp(method, "close") == 0) {
                if (node->as.call.arg_count != 1) {
                    compile_error(c, node->line, "stream.close() takes exactly 1 argument");
                    return;
                }
                compile_expr(c, node->as.call.args[0]);
                emit_op(c, OP_STREAM_CLOSE, node->line);
                return;
            }
            compile_error(c, node->line, "unknown stream method");
            return;
        }

        /* Not a known module - fall through to regular method call */
    }

    if (callee->type == NODE_IDENT) {
        const char *name = callee->as.ident.name;

        /* print(x) -> OP_PRINT */
        if (strcmp(name, "print") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "print() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_PRINT, node->line);
            emit_op(c, OP_NIL, node->line); /* print returns nil */
            return;
        }

        /* len(x) -> OP_LEN */
        if (strcmp(name, "len") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "len() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_LEN, node->line);
            return;
        }

        /* type(x) -> OP_TYPE */
        if (strcmp(name, "type") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "type() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TYPE, node->line);
            return;
        }

        /* keys(map) -> OP_KEYS */
        if (strcmp(name, "keys") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "keys() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_KEYS, node->line);
            return;
        }

        /* push(arr, val) -> OP_PUSH */
        if (strcmp(name, "push") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "push() takes exactly 2 arguments");
                return;
            }
            AstNode *arr_arg = node->as.call.args[0];
            compile_expr(c, arr_arg);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_PUSH, node->line);
            /* OP_PUSH leaves modified array on stack. We need to:
             * 1. Update the original variable for COW correctness
             * 2. Pop the array since push() returns nil */
            if (arr_arg->type == NODE_IDENT) {
                int slot = resolve_local(c, arr_arg->as.ident.name, strlen(arr_arg->as.ident.name));
                if (slot != -1) {
                    /* Local variable */
                    emit_op(c, OP_SET_LOCAL, node->line);
                    emit_bytes(c, (slot >> 8) & 0xFF, slot & 0xFF, node->line);
                } else {
                    /* Global variable */
                    size_t index = bytecode_add_string(c->code, arr_arg->as.ident.name);
                    emit_op(c, OP_SET_GLOBAL, node->line);
                    emit_byte(c, (index >> 8) & 0xFF, node->line);
                    emit_byte(c, index & 0xFF, node->line);
                }
            }
            emit_op(c, OP_POP, node->line);  /* Pop the array */
            emit_op(c, OP_NIL, node->line);  /* push() returns nil */
            return;
        }

        /* pop(arr) -> OP_POP_ARRAY */
        if (strcmp(name, "pop") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "pop() takes exactly 1 argument");
                return;
            }
            AstNode *arr_arg = node->as.call.args[0];
            compile_expr(c, arr_arg);
            emit_op(c, OP_POP_ARRAY, node->line);
            /* OP_POP_ARRAY pushes [popped_element, modified_array]
             * (modified_array is on top)
             * We need to:
             * 1. Update the original variable with modified array (for COW correctness)
             * 2. Pop the array, leaving popped_element as return value */
            if (arr_arg->type == NODE_IDENT) {
                int slot = resolve_local(c, arr_arg->as.ident.name, strlen(arr_arg->as.ident.name));
                if (slot != -1) {
                    /* Local variable */
                    emit_op(c, OP_SET_LOCAL, node->line);
                    emit_bytes(c, (slot >> 8) & 0xFF, slot & 0xFF, node->line);
                } else {
                    /* Global variable */
                    size_t index = bytecode_add_string(c->code, arr_arg->as.ident.name);
                    emit_op(c, OP_SET_GLOBAL, node->line);
                    emit_byte(c, (index >> 8) & 0xFF, node->line);
                    emit_byte(c, index & 0xFF, node->line);
                }
            }
            emit_op(c, OP_POP, node->line);  /* Pop the modified array, element remains */
            return;
        }

        /* slice(container, start, end) -> OP_SLICE */
        if (strcmp(name, "slice") == 0) {
            if (node->as.call.arg_count != 3) {
                compile_error(c, node->line, "slice() takes exactly 3 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            compile_expr(c, node->as.call.args[2]);
            emit_op(c, OP_SLICE, node->line);
            return;
        }

        /* str(x) -> OP_TO_STRING */
        if (strcmp(name, "str") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "str() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TO_STRING, node->line);
            return;
        }

        /* int(x) -> OP_TO_INT */
        if (strcmp(name, "int") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "int() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TO_INT, node->line);
            return;
        }

        /* float(x) -> OP_TO_FLOAT */
        if (strcmp(name, "float") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "float() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TO_FLOAT, node->line);
            return;
        }

        /* shell(command) -> OP_SHELL */
        if (strcmp(name, "shell") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "shell() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SHELL, node->line);
            return;
        }

        /* spawn(fn) -> OP_SPAWN */
        if (strcmp(name, "spawn") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "spawn() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SPAWN, node->line);
            return;
        }

        /* send(pid, value) -> OP_SEND */
        if (strcmp(name, "send") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "send() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_SEND, node->line);
            return;
        }

        /* receive() -> OP_RECEIVE */
        if (strcmp(name, "receive") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "receive() takes no arguments");
                return;
            }
            emit_op(c, OP_RECEIVE, node->line);
            return;
        }

        /* self() -> OP_SELF */
        if (strcmp(name, "self") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "self() takes no arguments");
                return;
            }
            emit_op(c, OP_SELF, node->line);
            return;
        }

        /* yield() -> OP_YIELD */
        if (strcmp(name, "yield") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "yield() takes no arguments");
                return;
            }
            emit_op(c, OP_YIELD, node->line);
            emit_op(c, OP_NIL, node->line); /* yield returns nil */
            return;
        }

        /* link(pid) -> OP_LINK (bidirectional crash notification) */
        if (strcmp(name, "link") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "link() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_LINK, node->line);
            return;
        }

        /* unlink(pid) -> OP_UNLINK */
        if (strcmp(name, "unlink") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "unlink() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_UNLINK, node->line);
            return;
        }

        /* monitor(pid) -> OP_MONITOR (unidirectional down notification) */
        if (strcmp(name, "monitor") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "monitor() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_MONITOR, node->line);
            return;
        }

        /* demonitor(pid) -> OP_DEMONITOR */
        if (strcmp(name, "demonitor") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "demonitor() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_DEMONITOR, node->line);
            return;
        }

        /* supervisor_start(strategy) -> OP_SUP_START */
        if (strcmp(name, "supervisor_start") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "supervisor_start() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SUP_START, node->line);
            return;
        }

        /* supervisor_add_child(name, fn, restart_strategy) -> OP_SUP_ADD_CHILD */
        if (strcmp(name, "supervisor_add_child") == 0) {
            if (node->as.call.arg_count != 3) {
                compile_error(c, node->line, "supervisor_add_child() takes exactly 3 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]); /* name */
            compile_expr(c, node->as.call.args[1]); /* function */
            compile_expr(c, node->as.call.args[2]); /* restart strategy */
            emit_op(c, OP_SUP_ADD_CHILD, node->line);
            return;
        }

        /* supervisor_remove_child(name) -> OP_SUP_REMOVE_CHILD */
        if (strcmp(name, "supervisor_remove_child") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "supervisor_remove_child() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SUP_REMOVE_CHILD, node->line);
            return;
        }

        /* supervisor_which_children() -> OP_SUP_WHICH_CHILDREN */
        if (strcmp(name, "supervisor_which_children") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "supervisor_which_children() takes no arguments");
                return;
            }
            emit_op(c, OP_SUP_WHICH_CHILDREN, node->line);
            return;
        }

        /* supervisor_shutdown() -> OP_SUP_SHUTDOWN */
        if (strcmp(name, "supervisor_shutdown") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "supervisor_shutdown() takes no arguments");
                return;
            }
            emit_op(c, OP_SUP_SHUTDOWN, node->line);
            return;
        }

        /* group_join(name) -> OP_GROUP_JOIN */
        if (strcmp(name, "group_join") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "group_join() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_GROUP_JOIN, node->line);
            return;
        }

        /* group_leave(name) -> OP_GROUP_LEAVE */
        if (strcmp(name, "group_leave") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "group_leave() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_GROUP_LEAVE, node->line);
            return;
        }

        /* group_send(name, message) -> OP_GROUP_SEND */
        if (strcmp(name, "group_send") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "group_send() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_GROUP_SEND, node->line);
            return;
        }

        /* group_send_others(name, message) -> OP_GROUP_SEND_OTHERS */
        if (strcmp(name, "group_send_others") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "group_send_others() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_GROUP_SEND_OTHERS, node->line);
            return;
        }

        /* group_members(name) -> OP_GROUP_MEMBERS */
        if (strcmp(name, "group_members") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "group_members() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_GROUP_MEMBERS, node->line);
            return;
        }

        /* group_list() -> OP_GROUP_LIST */
        if (strcmp(name, "group_list") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "group_list() takes no arguments");
                return;
            }
            emit_op(c, OP_GROUP_LIST, node->line);
            return;
        }

        /* get_stats(pid) -> OP_GET_STATS */
        if (strcmp(name, "get_stats") == 0) {
            if (node->as.call.arg_count == 0) {
                /* Default to self */
                emit_op(c, OP_NIL, node->line);
            } else if (node->as.call.arg_count == 1) {
                compile_expr(c, node->as.call.args[0]);
            } else {
                compile_error(c, node->line, "get_stats() takes 0 or 1 argument");
                return;
            }
            emit_op(c, OP_GET_STATS, node->line);
            return;
        }

        /* trace(pid, flags) -> OP_TRACE */
        if (strcmp(name, "trace") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "trace() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_TRACE, node->line);
            return;
        }

        /* trace_off(pid) -> OP_TRACE_OFF */
        if (strcmp(name, "trace_off") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "trace_off() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TRACE_OFF, node->line);
            return;
        }

        /* receive_match(pattern) -> OP_RECEIVE_MATCH */
        if (strcmp(name, "receive_match") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "receive_match() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_RECEIVE_MATCH, node->line);
            return;
        }

        /* sleep(ms) -> OP_SLEEP */
        if (strcmp(name, "sleep") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "sleep() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SLEEP, node->line);
            return;
        }

        /* time() -> OP_TIME */
        if (strcmp(name, "time") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "time() takes no arguments");
                return;
            }
            emit_op(c, OP_TIME, node->line);
            return;
        }

        /* time_format(ts, fmt) -> OP_TIME_FORMAT */
        if (strcmp(name, "time_format") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "time_format() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_TIME_FORMAT, node->line);
            return;
        }

        /* random() -> OP_RANDOM */
        if (strcmp(name, "random") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "random() takes no arguments");
                return;
            }
            emit_op(c, OP_RANDOM, node->line);
            return;
        }

        /* random_int(min, max) -> OP_RANDOM_INT */
        if (strcmp(name, "random_int") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "random_int() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_RANDOM_INT, node->line);
            return;
        }

        /* split(str, delim) -> OP_SPLIT */
        if (strcmp(name, "split") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "split() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_SPLIT, node->line);
            return;
        }

        /* join(arr, delim) -> OP_JOIN */
        if (strcmp(name, "join") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "join() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_JOIN, node->line);
            return;
        }

        /* trim(str) -> OP_TRIM */
        if (strcmp(name, "trim") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "trim() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TRIM, node->line);
            return;
        }

        /* replace(str, search, replacement) -> OP_REPLACE */
        if (strcmp(name, "replace") == 0) {
            if (node->as.call.arg_count != 3) {
                compile_error(c, node->line, "replace() takes exactly 3 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            compile_expr(c, node->as.call.args[2]);
            emit_op(c, OP_REPLACE, node->line);
            return;
        }

        /* contains(haystack, needle) -> OP_CONTAINS */
        if (strcmp(name, "contains") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "contains() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_CONTAINS, node->line);
            return;
        }

        /* starts_with(str, prefix) -> OP_STARTS_WITH */
        if (strcmp(name, "starts_with") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "starts_with() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_STARTS_WITH, node->line);
            return;
        }

        /* ends_with(str, suffix) -> OP_ENDS_WITH */
        if (strcmp(name, "ends_with") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "ends_with() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_ENDS_WITH, node->line);
            return;
        }

        /* upper(str) -> OP_UPPER */
        if (strcmp(name, "upper") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "upper() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_UPPER, node->line);
            return;
        }

        /* lower(str) -> OP_LOWER */
        if (strcmp(name, "lower") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "lower() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_LOWER, node->line);
            return;
        }

        /* char_at(str, idx) -> OP_CHAR_AT */
        if (strcmp(name, "char_at") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "char_at() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_CHAR_AT, node->line);
            return;
        }

        /* index_of(haystack, needle) -> OP_INDEX_OF */
        if (strcmp(name, "index_of") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "index_of() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_INDEX_OF, node->line);
            return;
        }

        /* base64_encode(str) -> OP_BASE64_ENCODE */
        if (strcmp(name, "base64_encode") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "base64_encode() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_BASE64_ENCODE, node->line);
            return;
        }

        /* base64_decode(str) -> OP_BASE64_DECODE */
        if (strcmp(name, "base64_decode") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "base64_decode() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_BASE64_DECODE, node->line);
            return;
        }

        /* read_stdin() -> OP_READ_STDIN */
        if (strcmp(name, "read_stdin") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "read_stdin() takes no arguments");
                return;
            }
            emit_op(c, OP_READ_STDIN, node->line);
            return;
        }

        /* print_err(val) -> OP_PRINT_ERR */
        if (strcmp(name, "print_err") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "print_err() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_PRINT_ERR, node->line);
            return;
        }

        /* floor(n) -> OP_FLOOR */
        if (strcmp(name, "floor") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "floor() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_FLOOR, node->line);
            return;
        }

        /* ceil(n) -> OP_CEIL */
        if (strcmp(name, "ceil") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "ceil() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_CEIL, node->line);
            return;
        }

        /* round(n) -> OP_ROUND */
        if (strcmp(name, "round") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "round() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_ROUND, node->line);
            return;
        }

        /* abs(n) -> OP_ABS */
        if (strcmp(name, "abs") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "abs() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_ABS, node->line);
            return;
        }

        /* sqrt(n) -> OP_SQRT */
        if (strcmp(name, "sqrt") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "sqrt() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_SQRT, node->line);
            return;
        }

        /* pow(base, exp) -> OP_POW */
        if (strcmp(name, "pow") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "pow() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_POW, node->line);
            return;
        }

        /* min(a, b) -> OP_MIN */
        if (strcmp(name, "min") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "min() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_MIN, node->line);
            return;
        }

        /* max(a, b) -> OP_MAX */
        if (strcmp(name, "max") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "max() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_MAX, node->line);
            return;
        }

        /* Process execution functions */

        /* exec(cmd, input) -> OP_EXEC */
        if (strcmp(name, "exec") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "exec() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_EXEC, node->line);
            return;
        }

        /* exec_async(cmd) -> OP_EXEC_ASYNC */
        if (strcmp(name, "exec_async") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "exec_async() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_EXEC_ASYNC, node->line);
            return;
        }

        /* proc_write(handle, data) -> OP_PROC_WRITE */
        if (strcmp(name, "proc_write") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "proc_write() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_PROC_WRITE, node->line);
            return;
        }

        /* proc_read(handle) -> OP_PROC_READ */
        if (strcmp(name, "proc_read") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "proc_read() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_PROC_READ, node->line);
            return;
        }

        /* proc_close(handle) -> OP_PROC_CLOSE */
        if (strcmp(name, "proc_close") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "proc_close() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_PROC_CLOSE, node->line);
            return;
        }

        /* UUID function */

        /* uuid() -> OP_UUID */
        if (strcmp(name, "uuid") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "uuid() takes no arguments");
                return;
            }
            emit_op(c, OP_UUID, node->line);
            return;
        }

        /* Hashing functions */

        /* hash_md5(str) -> OP_HASH_MD5 */
        if (strcmp(name, "hash_md5") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "hash_md5() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_HASH_MD5, node->line);
            return;
        }

        /* hash_sha256(str) -> OP_HASH_SHA256 */
        if (strcmp(name, "hash_sha256") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "hash_sha256() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_HASH_SHA256, node->line);
            return;
        }

        /* Result handling functions */

        /* is_ok(result) -> OP_RESULT_IS_OK */
        if (strcmp(name, "is_ok") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "is_ok() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_RESULT_IS_OK, node->line);
            return;
        }

        /* is_err(result) -> OP_RESULT_IS_ERR */
        if (strcmp(name, "is_err") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "is_err() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_RESULT_IS_ERR, node->line);
            return;
        }

        /* unwrap(result) -> OP_RESULT_UNWRAP */
        if (strcmp(name, "unwrap") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "unwrap() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_RESULT_UNWRAP, node->line);
            return;
        }

        /* unwrap_or(result, default) -> OP_RESULT_UNWRAP_OR */
        if (strcmp(name, "unwrap_or") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "unwrap_or() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_RESULT_UNWRAP_OR, node->line);
            return;
        }

        /* Option handling functions */

        /* is_some(option) -> OP_IS_SOME */
        if (strcmp(name, "is_some") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "is_some() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_IS_SOME, node->line);
            return;
        }

        /* is_none(option) -> OP_IS_NONE */
        if (strcmp(name, "is_none") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "is_none() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_IS_NONE, node->line);
            return;
        }

        /* unwrap_option(option) -> OP_UNWRAP_OPTION */
        if (strcmp(name, "unwrap_option") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "unwrap_option() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_UNWRAP_OPTION, node->line);
            return;
        }

        /* unwrap_option_or(option, default) -> OP_UNWRAP_OPTION_OR */
        if (strcmp(name, "unwrap_option_or") == 0) {
            if (node->as.call.arg_count != 2) {
                compile_error(c, node->line, "unwrap_option_or() takes exactly 2 arguments");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            compile_expr(c, node->as.call.args[1]);
            emit_op(c, OP_UNWRAP_OPTION_OR, node->line);
            return;
        }

        /* Tool introspection functions */

        /* list_tools() -> OP_LIST_TOOLS */
        if (strcmp(name, "list_tools") == 0) {
            if (node->as.call.arg_count != 0) {
                compile_error(c, node->line, "list_tools() takes no arguments");
                return;
            }
            emit_op(c, OP_LIST_TOOLS, node->line);
            return;
        }

        /* tool_schema(name) -> OP_TOOL_SCHEMA */
        if (strcmp(name, "tool_schema") == 0) {
            if (node->as.call.arg_count != 1) {
                compile_error(c, node->line, "tool_schema() takes exactly 1 argument");
                return;
            }
            compile_expr(c, node->as.call.args[0]);
            emit_op(c, OP_TOOL_SCHEMA, node->line);
            return;
        }
    }

    /* Regular function call */
    compile_expr(c, callee);

    for (size_t i = 0; i < node->as.call.arg_count; i++) {
        compile_expr(c, node->as.call.args[i]);
    }

    emit_op(c, OP_CALL, node->line);
    emit_byte(c, (node->as.call.arg_count >> 8) & 0xFF, node->line);
    emit_byte(c, node->as.call.arg_count & 0xFF, node->line);
}

static void compile_member(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.member.object);

    /* Use inline cache for property access */
    size_t key_idx = bytecode_add_string(c->code, node->as.member.field);
    size_t ic_slot = chunk_alloc_ic(current_chunk(c));

    emit_op(c, OP_MAP_GET_IC, node->line);
    emit_byte(c, (key_idx >> 8) & 0xFF, node->line);
    emit_byte(c, key_idx & 0xFF, node->line);
    emit_byte(c, (ic_slot >> 8) & 0xFF, node->line);
    emit_byte(c, ic_slot & 0xFF, node->line);
}

static void compile_index(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.index_expr.object);
    compile_expr(c, node->as.index_expr.index);
    /* Determine if array or map access based on index type at runtime */
    /* For now, use ARRAY_GET - VM can handle both */
    emit_op(c, OP_ARRAY_GET, node->line);
}

static void compile_ternary(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.ternary.cond);
    size_t else_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);
    compile_expr(c, node->as.ternary.then_expr);
    size_t end_jump = emit_jump(c, OP_JUMP, node->line);
    patch_jump(c, else_jump);
    emit_op(c, OP_POP, node->line);
    compile_expr(c, node->as.ternary.else_expr);
    patch_jump(c, end_jump);
}

static void compile_assign(Compiler *c, AstNode *node) {
    AstNode *target = node->as.assign.target;
    TokenType op = node->as.assign.op;

    if (target->type == NODE_IDENT) {
        /* Variable assignment: compile value first */
        if (op != TOK_ASSIGN) {
            /* Compound assignment: target op= value -> target = target op value */
            compile_expr(c, target);
            compile_expr(c, node->as.assign.value);
            switch (op) {
            case TOK_PLUS_ASSIGN: emit_op(c, OP_ADD, node->line); break;
            case TOK_MINUS_ASSIGN: emit_op(c, OP_SUB, node->line); break;
            case TOK_STAR_ASSIGN: emit_op(c, OP_MUL, node->line); break;
            case TOK_SLASH_ASSIGN: emit_op(c, OP_DIV, node->line); break;
            default: break;
            }
        } else {
            compile_expr(c, node->as.assign.value);
        }

        const char *name = target->as.ident.name;
        size_t length = strlen(name);
        int slot = resolve_local(c, name, length);

        if (slot >= 0) {
            if (c->current->locals[slot].is_const) {
                compile_error(c, node->line, "cannot assign to constant");
                return;
            }
            emit_op(c, OP_SET_LOCAL, node->line);
            emit_byte(c, (slot >> 8) & 0xFF, node->line);
            emit_byte(c, slot & 0xFF, node->line);
        } else {
            size_t index = bytecode_add_string(c->code, name);
            emit_op(c, OP_SET_GLOBAL, node->line);
            emit_byte(c, (index >> 8) & 0xFF, node->line);
            emit_byte(c, index & 0xFF, node->line);
        }
    } else if (target->type == NODE_INDEX) {
        /* Index assignment: need stack [array, index, value] */
        compile_expr(c, target->as.index_expr.object);
        compile_expr(c, target->as.index_expr.index);

        if (op != TOK_ASSIGN) {
            /* Compound index assignment: arr[i] op= value
             * Stack: [arr, idx]
             * DUP2:  [arr, idx, arr, idx]
             * GET:   [arr, idx, current]
             * value: [arr, idx, current, rhs]
             * op:    [arr, idx, new_val]
             * SET:   [arr] (result)
             */
            emit_op(c, OP_DUP2, node->line);
            emit_op(c, OP_ARRAY_GET, node->line);
            compile_expr(c, node->as.assign.value);
            switch (op) {
            case TOK_PLUS_ASSIGN: emit_op(c, OP_ADD, node->line); break;
            case TOK_MINUS_ASSIGN: emit_op(c, OP_SUB, node->line); break;
            case TOK_STAR_ASSIGN: emit_op(c, OP_MUL, node->line); break;
            case TOK_SLASH_ASSIGN: emit_op(c, OP_DIV, node->line); break;
            default: break;
            }
            emit_op(c, OP_ARRAY_SET, node->line);
            return;
        }

        compile_expr(c, node->as.assign.value);
        emit_op(c, OP_ARRAY_SET, node->line);
    } else if (target->type == NODE_MEMBER) {
        /* Member assignment: need stack [map, key, value] */
        compile_expr(c, target->as.member.object);
        emit_constant(c, value_string(target->as.member.field), node->line);

        if (op != TOK_ASSIGN) {
            /* Compound member assignment: obj.field op= value
             * Stack: [obj, "field"]
             * DUP2:  [obj, "field", obj, "field"]
             * GET:   [obj, "field", current]
             * value: [obj, "field", current, rhs]
             * op:    [obj, "field", new_val]
             * SET:   [obj] (result)
             */
            emit_op(c, OP_DUP2, node->line);
            emit_op(c, OP_MAP_GET, node->line);
            compile_expr(c, node->as.assign.value);
            switch (op) {
            case TOK_PLUS_ASSIGN: emit_op(c, OP_ADD, node->line); break;
            case TOK_MINUS_ASSIGN: emit_op(c, OP_SUB, node->line); break;
            case TOK_STAR_ASSIGN: emit_op(c, OP_MUL, node->line); break;
            case TOK_SLASH_ASSIGN: emit_op(c, OP_DIV, node->line); break;
            default: break;
            }
            emit_op(c, OP_MAP_SET, node->line);
            return;
        }

        compile_expr(c, node->as.assign.value);
        emit_op(c, OP_MAP_SET, node->line);
    } else {
        compile_error(c, node->line, "invalid assignment target");
    }
}

static void compile_array(Compiler *c, AstNode *node) {
    emit_op(c, OP_ARRAY_NEW, node->line);
    for (size_t i = 0; i < node->as.array.count; i++) {
        compile_expr(c, node->as.array.elements[i]);
        emit_op(c, OP_ARRAY_PUSH, node->line);
    }
}

static void compile_map(Compiler *c, AstNode *node) {
    emit_op(c, OP_MAP_NEW, node->line);
    for (size_t i = 0; i < node->as.map.count; i++) {
        emit_constant(c, value_string(node->as.map.keys[i]), node->line);
        compile_expr(c, node->as.map.values[i]);
        emit_op(c, OP_MAP_SET, node->line);
    }
}

static void compile_result_ok(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.result_expr.value);
    emit_op(c, OP_RESULT_OK, node->line);
}

static void compile_result_err(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.result_expr.value);
    emit_op(c, OP_RESULT_ERR, node->line);
}

static void compile_some(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.some_expr.value);
    emit_op(c, OP_SOME, node->line);
}

static void compile_none(Compiler *c, AstNode *node) {
    emit_op(c, OP_NONE, node->line);
}

static void compile_struct_decl(Compiler *c, AstNode *node) {
    /*
     * Struct declarations are type-only, no runtime code needed.
     * The type information is used for type checking and struct initialization.
     */
    (void)c;
    (void)node;
}

static void compile_struct_init(Compiler *c, AstNode *node) {
    /*
     * Struct initialization: Point { x: 1, y: 2 }
     *
     * Compiles to:
     *   <push field values in order>
     *   OP_STRUCT_NEW [type_name_idx:16][field_count:8][field_name_idx:16]...
     */
    size_t field_count = node->as.struct_init.field_count;

    /* Compile each field value in order */
    for (size_t i = 0; i < field_count; i++) {
        compile_expr(c, node->as.struct_init.field_values[i]);
    }

    /* Emit OP_STRUCT_NEW with type name and field count */
    size_t type_idx = bytecode_add_string(c->code, node->as.struct_init.type_name);
    emit_op(c, OP_STRUCT_NEW, node->line);
    emit_byte(c, (type_idx >> 8) & 0xFF, node->line);
    emit_byte(c, type_idx & 0xFF, node->line);
    emit_byte(c, (uint8_t)field_count, node->line);

    /* Emit field names in reverse order (VM reads them as i goes from field_count-1 to 0) */
    for (int i = (int)field_count - 1; i >= 0; i--) {
        size_t field_idx = bytecode_add_string(c->code, node->as.struct_init.field_names[i]);
        emit_byte(c, (field_idx >> 8) & 0xFF, node->line);
        emit_byte(c, field_idx & 0xFF, node->line);
    }
}

static void compile_enum_decl(Compiler *c, AstNode *node) {
    /*
     * Enum declarations are type-only, no runtime code needed.
     * The type information is used for type checking and enum creation.
     */
    (void)c;
    (void)node;
}

static void compile_enum_expr(Compiler *c, AstNode *node) {
    /*
     * Enum variant expression: EnumType::Variant or EnumType::Variant(payload)
     *
     * Compiles to:
     *   [payload if present]
     *   OP_ENUM_NEW [type_idx:16][variant_idx:16][has_payload:8]
     */
    bool has_payload = (node->as.enum_expr.payload != NULL);

    /* Compile payload if present */
    if (has_payload) {
        compile_expr(c, node->as.enum_expr.payload);
    }

    /* Emit OP_ENUM_NEW */
    size_t type_idx = bytecode_add_string(c->code, node->as.enum_expr.enum_type);
    size_t variant_idx = bytecode_add_string(c->code, node->as.enum_expr.variant_name);

    emit_op(c, OP_ENUM_NEW, node->line);
    emit_byte(c, (type_idx >> 8) & 0xFF, node->line);
    emit_byte(c, type_idx & 0xFF, node->line);
    emit_byte(c, (variant_idx >> 8) & 0xFF, node->line);
    emit_byte(c, variant_idx & 0xFF, node->line);
    emit_byte(c, has_payload ? 1 : 0, node->line);
}

static void compile_try(Compiler *c, AstNode *node) {
    /*
     * try expr
     *
     * Compiles to:
     *   expr                    ; evaluate the expression
     *   OP_DUP                  ; duplicate for checking
     *   OP_RESULT_IS_ERR        ; check if it's an error
     *   OP_JUMP_UNLESS end      ; if ok, skip to end
     *   OP_RETURN               ; if err, return early with the error
     * end:
     *   OP_RESULT_UNWRAP        ; unwrap the ok value
     */
    compile_expr(c, node->as.try_expr.expr);
    emit_op(c, OP_DUP, node->line);
    emit_op(c, OP_RESULT_IS_ERR, node->line);
    size_t end_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);  /* Pop the bool */
    emit_op(c, OP_RETURN, node->line);  /* Return the error result */
    patch_jump(c, end_jump);
    emit_op(c, OP_POP, node->line);  /* Pop the bool */
    emit_op(c, OP_RESULT_UNWRAP, node->line);
}

/**
 * Check if a node is or contains a return statement.
 * Used to optimize match arm compilation.
 */
static bool is_return_statement(AstNode *node) {
    if (!node) return false;
    if (node->type == NODE_RETURN) return true;
    /* Check if block that ends with return */
    if (node->type == NODE_BLOCK && node->as.block.count > 0) {
        AstNode *last = node->as.block.stmts[node->as.block.count - 1];
        return is_return_statement(last);
    }
    return false;
}

/**
 * Compile a match arm body, handling return statements specially.
 * When body is a return, we skip the normal cleanup since the function exits.
 */
static void compile_match_arm_body(Compiler *c, AstNode *arm, bool has_binding) {
    AstNode *body = arm->as.match_arm.body;

    if (is_return_statement(body)) {
        /* Body is a return statement - compile it directly */
        /* Return handles everything, no cleanup needed */
        if (body->type == NODE_RETURN) {
            compile_return(c, body);
        } else {
            /* Block ending with return */
            compile_block_expr(c, body);
        }
    } else if (body->type == NODE_BLOCK) {
        /* Block body - compile as expression keeping last value */
        compile_block_expr(c, body);
        /* Remove the binding but keep the body result */
        if (has_binding) {
            emit_op(c, OP_SWAP, arm->line);
            emit_op(c, OP_POP, arm->line);
        }
    } else {
        /* Normal expression body */
        compile_expr(c, body);
        /* Remove the binding but keep the body result */
        if (has_binding) {
            emit_op(c, OP_SWAP, arm->line);
            emit_op(c, OP_POP, arm->line);
        }
    }
}

static void compile_match(Compiler *c, AstNode *node) {
    /*
     * Match on Result:
     *   match result {
     *     ok(x) => body1
     *     err(e) => body2
     *   }
     *
     * Match on Option:
     *   match option {
     *     some(x) => body1
     *     none => body2
     *   }
     */
    compile_expr(c, node->as.match_expr.expr);

    /* Find arms by pattern kind */
    AstNode *ok_arm = NULL;
    AstNode *err_arm = NULL;
    AstNode *some_arm = NULL;
    AstNode *none_arm = NULL;
    bool has_enum_arms = false;

    for (size_t i = 0; i < node->as.match_expr.arm_count; i++) {
        AstNode *arm = node->as.match_expr.arms[i];
        switch (arm->as.match_arm.pattern_kind) {
        case MATCH_PATTERN_OK:
            ok_arm = arm;
            break;
        case MATCH_PATTERN_ERR:
            err_arm = arm;
            break;
        case MATCH_PATTERN_SOME:
            some_arm = arm;
            break;
        case MATCH_PATTERN_NONE:
            none_arm = arm;
            break;
        case MATCH_PATTERN_ENUM:
            has_enum_arms = true;
            break;
        }
    }

    /* Determine if this is a Result, Option, or Enum match */
    bool is_result_match = (ok_arm != NULL || err_arm != NULL);
    bool is_option_match = (some_arm != NULL || none_arm != NULL);

    if ((is_result_match && is_option_match) ||
        (is_result_match && has_enum_arms) ||
        (is_option_match && has_enum_arms)) {
        compile_error(c, node->line, "cannot mix different pattern types in match");
        return;
    }

    if (is_result_match) {
        /* Result match: ok/err */
        if (!ok_arm || !err_arm) {
            compile_error(c, node->line, "match expression must have both ok and err arms");
            return;
        }

        /* Check if ok */
        emit_op(c, OP_DUP, node->line);
        emit_op(c, OP_RESULT_IS_OK, node->line);
        size_t err_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);

        /* Ok arm */
        emit_op(c, OP_POP, node->line);  /* Pop bool */
        emit_op(c, OP_RESULT_UNWRAP, node->line);  /* Get ok value */

        begin_scope(c);
        const char *ok_name = ok_arm->as.match_arm.binding_name;
        add_local(c, ok_name, strlen(ok_name), true, ok_arm->line);
        compile_match_arm_body(c, ok_arm, true);
        c->current->local_count--;
        agim_free(c->current->locals[c->current->local_count].name);
        c->current->scope_depth--;

        size_t end_jump = emit_jump(c, OP_JUMP, node->line);

        /* Err arm */
        patch_jump(c, err_jump);
        emit_op(c, OP_POP, node->line);  /* Pop bool */
        emit_op(c, OP_RESULT_UNWRAP, node->line);  /* Get err value */

        begin_scope(c);
        const char *err_name = err_arm->as.match_arm.binding_name;
        add_local(c, err_name, strlen(err_name), true, err_arm->line);
        compile_match_arm_body(c, err_arm, true);
        c->current->local_count--;
        agim_free(c->current->locals[c->current->local_count].name);
        c->current->scope_depth--;

        patch_jump(c, end_jump);
    } else if (is_option_match) {
        /* Option match: some/none */
        if (!some_arm || !none_arm) {
            compile_error(c, node->line, "match expression must have both some and none arms");
            return;
        }

        /* Check if some */
        emit_op(c, OP_DUP, node->line);
        emit_op(c, OP_IS_SOME, node->line);
        size_t none_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);

        /* Some arm */
        emit_op(c, OP_POP, node->line);  /* Pop bool */
        emit_op(c, OP_UNWRAP_OPTION, node->line);  /* Get some value */

        begin_scope(c);
        const char *some_name = some_arm->as.match_arm.binding_name;
        add_local(c, some_name, strlen(some_name), true, some_arm->line);
        compile_match_arm_body(c, some_arm, true);
        c->current->local_count--;
        agim_free(c->current->locals[c->current->local_count].name);
        c->current->scope_depth--;

        size_t end_jump = emit_jump(c, OP_JUMP, node->line);

        /* None arm */
        patch_jump(c, none_jump);
        emit_op(c, OP_POP, node->line);  /* Pop bool */
        emit_op(c, OP_POP, node->line);  /* Pop the option value (none has no binding) */

        compile_match_arm_body(c, none_arm, false);

        patch_jump(c, end_jump);
    } else if (has_enum_arms) {
        /*
         * Enum match: check each variant in sequence
         *
         * For each arm:
         *   DUP                    ; keep enum value for next check
         *   OP_ENUM_IS [variant]   ; check if matches this variant
         *   JUMP_UNLESS next_arm   ; skip if not matching
         *   POP                    ; pop the bool
         *   [extract payload if binding]
         *   body
         *   JUMP end
         * next_arm:
         *   POP                    ; pop the bool
         *   ... repeat ...
         * end:
         */
        size_t arm_count = node->as.match_expr.arm_count;
        size_t *end_jumps = agim_alloc(sizeof(size_t) * arm_count);
        size_t end_jump_count = 0;

        for (size_t i = 0; i < arm_count; i++) {
            AstNode *arm = node->as.match_expr.arms[i];
            if (arm->as.match_arm.pattern_kind != MATCH_PATTERN_ENUM) continue;

            const char *variant = arm->as.match_arm.variant_name;
            size_t variant_idx = bytecode_add_string(c->code, variant);

            /* Check if enum matches this variant */
            emit_op(c, OP_DUP, node->line);  /* Keep enum for potential next check */
            emit_op(c, OP_ENUM_IS, node->line);
            emit_byte(c, (variant_idx >> 8) & 0xFF, node->line);
            emit_byte(c, variant_idx & 0xFF, node->line);

            size_t next_arm_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);

            /* Match found: pop bool, process body */
            emit_op(c, OP_POP, node->line);  /* Pop bool */

            if (arm->as.match_arm.binding_name) {
                /* Has payload binding: extract payload */
                emit_op(c, OP_ENUM_PAYLOAD, node->line);

                begin_scope(c);
                const char *binding = arm->as.match_arm.binding_name;
                add_local(c, binding, strlen(binding), true, arm->line);
                compile_match_arm_body(c, arm, true);
                c->current->local_count--;
                agim_free(c->current->locals[c->current->local_count].name);
                c->current->scope_depth--;
            } else {
                /* No binding: just pop the enum and compile body */
                emit_op(c, OP_POP, node->line);  /* Pop enum value */
                compile_match_arm_body(c, arm, false);
            }

            /* Jump to end */
            end_jumps[end_jump_count++] = emit_jump(c, OP_JUMP, node->line);

            /* Not matched: jump here */
            patch_jump(c, next_arm_jump);
            emit_op(c, OP_POP, node->line);  /* Pop bool */
        }

        /* If no arm matched, pop the enum value and push nil */
        emit_op(c, OP_POP, node->line);  /* Pop enum value */
        emit_op(c, OP_NIL, node->line);  /* Default result */

        /* Patch all end jumps */
        for (size_t i = 0; i < end_jump_count; i++) {
            patch_jump(c, end_jumps[i]);
        }

        agim_free(end_jumps);
    } else {
        compile_error(c, node->line, "match expression must have ok/err, some/none, or enum variant arms");
    }
}

static void compile_expr(Compiler *c, AstNode *node) {
    if (c->had_error) return;

    switch (node->type) {
    case NODE_NIL:
    case NODE_BOOL:
    case NODE_INT:
    case NODE_FLOAT:
    case NODE_STRING:
        compile_literal(c, node);
        break;
    case NODE_IDENT:
        compile_ident(c, node);
        break;
    case NODE_BINARY:
        compile_binary(c, node);
        break;
    case NODE_UNARY:
        compile_unary(c, node);
        break;
    case NODE_CALL:
        compile_call(c, node);
        break;
    case NODE_MEMBER:
        compile_member(c, node);
        break;
    case NODE_INDEX:
        compile_index(c, node);
        break;
    case NODE_TERNARY:
        compile_ternary(c, node);
        break;
    case NODE_ASSIGN:
        compile_assign(c, node);
        break;
    case NODE_ARRAY:
        compile_array(c, node);
        break;
    case NODE_MAP:
        compile_map(c, node);
        break;
    case NODE_RESULT_OK:
        compile_result_ok(c, node);
        break;
    case NODE_RESULT_ERR:
        compile_result_err(c, node);
        break;
    case NODE_SOME:
        compile_some(c, node);
        break;
    case NODE_NONE:
        compile_none(c, node);
        break;
    case NODE_TRY:
        compile_try(c, node);
        break;
    case NODE_MATCH:
        compile_match(c, node);
        break;
    case NODE_STRUCT_INIT:
        compile_struct_init(c, node);
        break;
    case NODE_ENUM_EXPR:
        compile_enum_expr(c, node);
        break;
    default:
        compile_error(c, node->line, "unexpected expression type");
        break;
    }
}

/* Statement Compilation */

static void compile_block(Compiler *c, AstNode *node) {
    begin_scope(c);
    for (size_t i = 0; i < node->as.block.count; i++) {
        compile_stmt(c, node->as.block.stmts[i]);
    }
    end_scope(c, node->line);
}

/* Compile block keeping last expression value on stack (for if/else expressions) */
static void compile_block_expr(Compiler *c, AstNode *node) {
    begin_scope(c);
    bool pushed_value = false;
    for (size_t i = 0; i < node->as.block.count; i++) {
        AstNode *stmt = node->as.block.stmts[i];
        bool is_last = (i == node->as.block.count - 1);

        if (is_last && stmt->type == NODE_EXPR_STMT) {
            /* Last expression: keep value on stack */
            compile_expr(c, stmt->as.return_stmt.value);
            pushed_value = true;
        } else {
            compile_stmt(c, stmt);
        }
    }
    /* If the block didn't end with an expression, push nil as the block's value */
    if (!pushed_value) {
        emit_op(c, OP_NIL, node->line);
    }
    end_scope(c, node->line);
}

static void compile_let(Compiler *c, AstNode *node, bool is_const) {
    compile_expr(c, node->as.var_decl.value);

    if (c->current->scope_depth > 0) {
        /* Local variable */
        add_local(c, node->as.var_decl.name, strlen(node->as.var_decl.name), is_const, node->line);
    } else {
        /* Global variable */
        size_t index = bytecode_add_string(c->code, node->as.var_decl.name);
        emit_op(c, OP_SET_GLOBAL, node->line);
        emit_byte(c, (index >> 8) & 0xFF, node->line);
        emit_byte(c, index & 0xFF, node->line);
        emit_op(c, OP_POP, node->line);
    }
}

static void compile_if(Compiler *c, AstNode *node) {
    compile_expr(c, node->as.if_stmt.cond);
    size_t else_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);

    /* Compile then branch as expression (keeps value on stack) */
    if (node->as.if_stmt.then_block->type == NODE_BLOCK) {
        compile_block_expr(c, node->as.if_stmt.then_block);
    } else {
        compile_expr(c, node->as.if_stmt.then_block);
    }

    size_t end_jump = emit_jump(c, OP_JUMP, node->line);

    patch_jump(c, else_jump);
    emit_op(c, OP_POP, node->line);

    if (node->as.if_stmt.else_block) {
        /* Compile else branch as expression */
        if (node->as.if_stmt.else_block->type == NODE_BLOCK) {
            compile_block_expr(c, node->as.if_stmt.else_block);
        } else if (node->as.if_stmt.else_block->type == NODE_IF) {
            /* Else-if chain */
            compile_if(c, node->as.if_stmt.else_block);
        } else {
            compile_expr(c, node->as.if_stmt.else_block);
        }
    } else {
        /* No else: push nil as the else value */
        emit_op(c, OP_NIL, node->line);
    }

    patch_jump(c, end_jump);
}

static void compile_while(Compiler *c, AstNode *node) {
    size_t loop_start = current_chunk(c)->code_size;
    begin_loop(c, loop_start);

    compile_expr(c, node->as.while_stmt.cond);
    size_t exit_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);

    compile_stmt(c, node->as.while_stmt.body);

    emit_loop(c, loop_start, node->line);

    patch_jump(c, exit_jump);
    emit_op(c, OP_POP, node->line);

    end_loop(c);
}

static void compile_for_range(Compiler *c, AstNode *node, AstNode *range) {
    /*
     * for i in start..end { body }    (exclusive)
     * for i in start..=end { body }   (inclusive)
     *
     * Compiles to:
     *   let __end = end
     *   let i = start
     *   while i < __end (or <= for inclusive) {
     *       body
     *       i = i + 1
     *   }
     */
    begin_scope(c);

    /* Evaluate end and store as __end */
    compile_expr(c, range->as.range.end);
    add_local(c, "__end", 5, true, node->line);
    int end_slot = resolve_local(c, "__end", 5);

    /* Initialize loop variable with start value */
    compile_expr(c, range->as.range.start);
    const char *var_name = node->as.for_stmt.var;
    add_local(c, var_name, strlen(var_name), false, node->line);
    int var_slot = resolve_local(c, var_name, strlen(var_name));

    /* Loop start */
    size_t loop_start = current_chunk(c)->code_size;
    begin_loop(c, loop_start);

    /* Condition: i < __end (or i <= __end for inclusive) */
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (var_slot >> 8) & 0xFF, var_slot & 0xFF, node->line);

    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (end_slot >> 8) & 0xFF, end_slot & 0xFF, node->line);

    if (range->as.range.inclusive) {
        emit_op(c, OP_LE, node->line);
    } else {
        emit_op(c, OP_LT, node->line);
    }

    size_t exit_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);

    /* Compile body (loop var is already in scope) */
    compile_stmt(c, node->as.for_stmt.body);

    /* i = i + 1 */
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (var_slot >> 8) & 0xFF, var_slot & 0xFF, node->line);
    emit_constant(c, value_int(1), node->line);
    emit_op(c, OP_ADD, node->line);
    emit_op(c, OP_SET_LOCAL, node->line);
    emit_bytes(c, (var_slot >> 8) & 0xFF, var_slot & 0xFF, node->line);
    emit_op(c, OP_POP, node->line);

    /* Loop back */
    emit_loop(c, loop_start, node->line);

    /* Exit point */
    patch_jump(c, exit_jump);
    emit_op(c, OP_POP, node->line);

    end_loop(c);
    end_scope(c, node->line);
}

static void compile_for(Compiler *c, AstNode *node) {
    /* Check if iterable is a range expression */
    if (node->as.for_stmt.iterable->type == NODE_RANGE) {
        compile_for_range(c, node, node->as.for_stmt.iterable);
        return;
    }

    /*
     * for item in iterable { body }
     *
     * Compiles to:
     *   let __iter = iterable
     *   let __idx = 0
     *   while __idx < len(__iter) {
     *       let item = __iter[__idx]
     *       body
     *       __idx = __idx + 1
     *   }
     */
    begin_scope(c);

    /* Evaluate iterable and store as __iter */
    compile_expr(c, node->as.for_stmt.iterable);
    add_local(c, "__iter", 6, true, node->line);
    int iter_slot = resolve_local(c, "__iter", 6);

    /* Initialize __idx = 0 */
    emit_constant(c, value_int(0), node->line);
    add_local(c, "__idx", 5, false, node->line);
    int idx_slot = resolve_local(c, "__idx", 5);

    /* Loop start */
    size_t loop_start = current_chunk(c)->code_size;
    begin_loop(c, loop_start);

    /* Condition: __idx < len(__iter) */
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (idx_slot >> 8) & 0xFF, idx_slot & 0xFF, node->line);

    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (iter_slot >> 8) & 0xFF, iter_slot & 0xFF, node->line);
    emit_op(c, OP_LEN, node->line);

    emit_op(c, OP_LT, node->line);

    size_t exit_jump = emit_jump(c, OP_JUMP_UNLESS, node->line);
    emit_op(c, OP_POP, node->line);

    /* Inner scope for loop variable */
    begin_scope(c);

    /* let item = __iter[__idx] */
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (iter_slot >> 8) & 0xFF, iter_slot & 0xFF, node->line);
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (idx_slot >> 8) & 0xFF, idx_slot & 0xFF, node->line);
    emit_op(c, OP_ARRAY_GET, node->line);

    const char *var_name = node->as.for_stmt.var;
    add_local(c, var_name, strlen(var_name), true, node->line);

    /* Compile body */
    compile_stmt(c, node->as.for_stmt.body);

    /* End inner scope (pops loop variable) */
    end_scope(c, node->line);

    /* __idx = __idx + 1 */
    emit_op(c, OP_GET_LOCAL, node->line);
    emit_bytes(c, (idx_slot >> 8) & 0xFF, idx_slot & 0xFF, node->line);
    emit_constant(c, value_int(1), node->line);
    emit_op(c, OP_ADD, node->line);
    emit_op(c, OP_SET_LOCAL, node->line);
    emit_bytes(c, (idx_slot >> 8) & 0xFF, idx_slot & 0xFF, node->line);
    emit_op(c, OP_POP, node->line);

    /* Loop back */
    emit_loop(c, loop_start, node->line);

    /* Exit point */
    patch_jump(c, exit_jump);
    emit_op(c, OP_POP, node->line);

    end_loop(c);
    end_scope(c, node->line);
}

static void compile_return(Compiler *c, AstNode *node) {
    if (node->as.return_stmt.value) {
        compile_expr(c, node->as.return_stmt.value);
    } else {
        emit_op(c, OP_NIL, node->line);
    }
    emit_op(c, OP_RETURN, node->line);
}

static void compile_stmt(Compiler *c, AstNode *node) {
    if (c->had_error) return;

    switch (node->type) {
    case NODE_BLOCK:
        compile_block(c, node);
        break;
    case NODE_LET:
        compile_let(c, node, false);
        break;
    case NODE_CONST:
        compile_let(c, node, true);
        break;
    case NODE_IF:
        compile_if(c, node);
        emit_op(c, OP_POP, node->line);  /* Pop if expression result when used as statement */
        break;
    case NODE_WHILE:
        compile_while(c, node);
        break;
    case NODE_FOR:
        compile_for(c, node);
        break;
    case NODE_RETURN:
        compile_return(c, node);
        break;
    case NODE_BREAK:
        emit_break(c, node->line);
        break;
    case NODE_CONTINUE:
        emit_continue(c, node->line);
        break;
    case NODE_EXPR_STMT:
        compile_expr(c, node->as.return_stmt.value);
        emit_op(c, OP_POP, node->line);
        break;
    default:
        compile_error(c, node->line, "unexpected statement type");
        break;
    }
}

/* Declaration Compilation */

static void compile_fn(Compiler *c, AstNode *node, bool is_tool) {
    /* Create new function chunk */
    Chunk *fn_chunk = chunk_new();
    size_t fn_index = bytecode_add_function(c->code, fn_chunk);

    /* Set up new function context */
    FunctionContext fn_ctx;
    fn_ctx.chunk = fn_chunk;
    fn_ctx.local_count = 0;
    fn_ctx.scope_depth = 0;
    fn_ctx.loop_depth = 0;
    fn_ctx.enclosing = c->current;
    c->current = &fn_ctx;

    begin_scope(c);

    /* Reserve slot 0 for the function itself */
    add_local(c, "", 0, true, node->line);

    /* Add parameters as locals */
    for (size_t i = 0; i < node->as.fn_decl.param_count; i++) {
        AstNode *param = node->as.fn_decl.params[i];
        add_local(c, param->as.param.name, strlen(param->as.param.name), false, param->line);
    }

    /* Compile body */
    AstNode *body = node->as.fn_decl.body;
    for (size_t i = 0; i < body->as.block.count; i++) {
        compile_stmt(c, body->as.block.stmts[i]);
    }

    /* Implicit return nil */
    emit_op(c, OP_NIL, node->line);
    emit_op(c, OP_RETURN, node->line);

    /* Restore context */
    c->current = fn_ctx.enclosing;

    /* Free locals */
    for (size_t i = 0; i < fn_ctx.local_count; i++) {
        agim_free(fn_ctx.locals[i].name);
    }

    /* Create function value and store as global */
    Value *fn_val = value_function(node->as.fn_decl.name, node->as.fn_decl.param_count);
    if (!fn_val) {
        compile_error(c, node->line, "failed to allocate function value");
        return;
    }
    fn_val->as.function->code_offset = fn_index;

    size_t const_idx = chunk_add_constant(current_chunk(c), fn_val);
    emit_op(c, OP_CONST, node->line);
    emit_bytes(c, (const_idx >> 8) & 0xFF, const_idx & 0xFF, node->line);

    size_t name_idx = bytecode_add_string(c->code, node->as.fn_decl.name);
    emit_op(c, OP_SET_GLOBAL, node->line);
    emit_bytes(c, (name_idx >> 8) & 0xFF, name_idx & 0xFF, node->line);
    emit_op(c, OP_POP, node->line);

    /* Register tool metadata */
    if (is_tool) {
        const char **param_names = NULL;
        const char **param_types = NULL;
        const char **param_descriptions = NULL;

        if (node->as.fn_decl.param_count > 0) {
            param_names = agim_alloc(sizeof(char *) * node->as.fn_decl.param_count);
            param_types = agim_alloc(sizeof(char *) * node->as.fn_decl.param_count);
            param_descriptions = agim_alloc(sizeof(char *) * node->as.fn_decl.param_count);

            /* Extract parameter descriptions from params_map if present */
            AstNode *params_map = node->as.fn_decl.params_map;

            for (size_t i = 0; i < node->as.fn_decl.param_count; i++) {
                AstNode *param = node->as.fn_decl.params[i];
                param_names[i] = param->as.param.name;
                param_descriptions[i] = NULL;

                /* Extract type name from type annotation node */
                if (param->as.param.type_ann && param->as.param.type_ann->type == NODE_TYPE_NAME) {
                    param_types[i] = param->as.param.type_ann->as.type_name.name;
                } else {
                    param_types[i] = NULL;
                }

                /* Look up description in params_map */
                if (params_map && params_map->type == NODE_MAP) {
                    for (size_t j = 0; j < params_map->as.map.count; j++) {
                        if (strcmp(params_map->as.map.keys[j], param->as.param.name) == 0) {
                            AstNode *desc_node = params_map->as.map.values[j];
                            if (desc_node && desc_node->type == NODE_STRING) {
                                param_descriptions[i] = desc_node->as.string_val;
                            }
                            break;
                        }
                    }
                }
            }
        }

        /* Extract return type name if present */
        const char *ret_type = NULL;
        if (node->as.fn_decl.return_type && node->as.fn_decl.return_type->type == NODE_TYPE_NAME) {
            ret_type = node->as.fn_decl.return_type->as.type_name.name;
        }

        bytecode_add_tool(c->code, node->as.fn_decl.name, fn_index,
                          param_names, param_types, param_descriptions,
                          node->as.fn_decl.param_count, ret_type,
                          node->as.fn_decl.description);

        if (param_names) agim_free(param_names);
        if (param_types) agim_free(param_types);
        if (param_descriptions) agim_free(param_descriptions);
    }
}

static void compile_module_decls(Compiler *c, Module *mod);

static void compile_import(Compiler *c, AstNode *node) {
    if (!c->module_cache) {
        c->module_cache = module_cache_new();
    }

    const char *path = node->as.import_stmt.path;

    /* Load the module */
    char *error = NULL;
    Module *mod = module_load(path, c->source_path, c->module_cache, &error);

    if (!mod) {
        compile_error(c, node->line, error ? error : "failed to load module");
        if (error) agim_free(error);
        return;
    }

    /* Compile the module's declarations if not already compiled */
    if (!mod->is_compiled) {
        mod->is_compiled = true;
        compile_module_decls(c, mod);
    }

    /* All exports are now available as globals in the bytecode */
}

static void compile_import_from(Compiler *c, AstNode *node) {
    if (!c->module_cache) {
        c->module_cache = module_cache_new();
    }

    const char *path = node->as.import_from.path;

    /* Load the module */
    char *error = NULL;
    Module *mod = module_load(path, c->source_path, c->module_cache, &error);

    if (!mod) {
        compile_error(c, node->line, error ? error : "failed to load module");
        if (error) agim_free(error);
        return;
    }

    /* Compile the module's declarations if not already compiled */
    if (!mod->is_compiled) {
        mod->is_compiled = true;
        compile_module_decls(c, mod);
    }

    /* Check that requested names are actually exported */
    for (size_t i = 0; i < node->as.import_from.name_count; i++) {
        const char *name = node->as.import_from.names[i];
        bool found = false;

        for (size_t j = 0; j < mod->export_count; j++) {
            if (strcmp(mod->exports[j], name) == 0) {
                found = true;
                break;
            }
        }

        if (!found) {
            char msg[256];
            snprintf(msg, sizeof(msg), "'%s' is not exported from module", name);
            compile_error(c, node->line, msg);
            return;
        }
    }
}

/**
 * Compile a module's declarations into the current bytecode.
 * This adds the module's exported symbols as globals.
 */
static void compile_module_decls(Compiler *c, Module *mod) {
    if (!mod || !mod->ast || mod->ast->type != NODE_PROGRAM) return;

    /* Save current source path and set module's path */
    char *saved_path = c->source_path;
    c->source_path = mod->path;

    /* Compile all declarations in the module */
    for (size_t i = 0; i < mod->ast->as.program.count; i++) {
        AstNode *decl = mod->ast->as.program.decls[i];

        /* Skip NODE_EXPORT wrapper and compile inner declaration */
        if (decl->type == NODE_EXPORT) {
            decl = decl->as.export_stmt.decl;
        }

        compile_decl(c, decl);

        if (c->had_error) {
            c->source_path = saved_path;
            return;
        }
    }

    /* Restore source path */
    c->source_path = saved_path;
}

static void compile_export(Compiler *c, AstNode *node) {
    /* Export just compiles the inner declaration normally.
     * The export marking is handled at the module level. */
    compile_decl(c, node->as.export_stmt.decl);
}

static void compile_decl(Compiler *c, AstNode *node) {
    if (c->had_error) return;

    switch (node->type) {
    case NODE_TOOL_DECL:
        compile_fn(c, node, true);
        break;
    case NODE_FN_DECL:
        compile_fn(c, node, false);
        break;
    case NODE_IMPORT:
        compile_import(c, node);
        break;
    case NODE_IMPORT_FROM:
        compile_import_from(c, node);
        break;
    case NODE_EXPORT:
        compile_export(c, node);
        break;
    case NODE_STRUCT_DECL:
        compile_struct_decl(c, node);
        break;
    case NODE_ENUM_DECL:
        compile_enum_decl(c, node);
        break;
    default:
        compile_stmt(c, node);
        break;
    }
}

static void compile_program(Compiler *c, AstNode *node) {
    for (size_t i = 0; i < node->as.program.count; i++) {
        AstNode *decl = node->as.program.decls[i];
        bool is_last = (i == node->as.program.count - 1);

        /* For the last expression/if statement, keep the value on stack */
        if (is_last && decl->type == NODE_EXPR_STMT) {
            compile_expr(c, decl->as.return_stmt.value);
        } else if (is_last && decl->type == NODE_IF) {
            /* If expression at end of program - don't pop result */
            compile_if(c, decl);
        } else {
            compile_decl(c, decl);
        }
    }

    /* Halt at end of main */
    emit_op(c, OP_HALT, node->line);
}

/* Public API */

Compiler *compiler_new(void) {
    Compiler *c = agim_alloc(sizeof(Compiler));
    if (!c) {
        LOG_ERROR("compiler: failed to allocate compiler");
        return NULL;
    }
    c->code = NULL;
    c->current = NULL;
    c->error = NULL;
    c->error_line = 0;
    c->had_error = false;
    c->module_cache = NULL;
    c->source_path = NULL;
    LOG_DEBUG("compiler: created new compiler instance");
    return c;
}

void compiler_free(Compiler *c) {
    if (c) {
        if (c->error) {
            agim_free(c->error);
        }
        if (c->module_cache) {
            module_cache_free(c->module_cache);
        }
        if (c->source_path) {
            agim_free(c->source_path);
        }
        agim_free(c);
    }
}

void compiler_set_source_path(Compiler *c, const char *path) {
    if (!c) return;
    if (c->source_path) {
        agim_free(c->source_path);
    }
    c->source_path = path ? agim_strdup(path) : NULL;
}

Bytecode *compiler_compile(Compiler *c, AstNode *ast) {
    if (!ast) return NULL;

    c->code = bytecode_new();
    c->had_error = false;

    /* Set up main function context */
    FunctionContext main_ctx;
    main_ctx.chunk = c->code->main;
    main_ctx.local_count = 0;
    main_ctx.scope_depth = 0;
    main_ctx.loop_depth = 0;
    main_ctx.enclosing = NULL;
    c->current = &main_ctx;

    compile_program(c, ast);

    /* Clean up main context locals */
    for (size_t i = 0; i < main_ctx.local_count; i++) {
        agim_free(main_ctx.locals[i].name);
    }

    if (c->had_error) {
        bytecode_free(c->code);
        return NULL;
    }

    Bytecode *result = c->code;
    c->code = NULL;
    return result;
}

const char *compiler_error(Compiler *c) {
    return c->error;
}

int compiler_error_line(Compiler *c) {
    return c->error_line;
}
