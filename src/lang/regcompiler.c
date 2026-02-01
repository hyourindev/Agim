/*
 * Agim - Register Bytecode Compiler Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/regcompiler.h"
#include "lang/regalloc.h"
#include "lang/ast.h"
#include "vm/value.h"
#include "util/alloc.h"
#include "debug/log.h"

#include <stdio.h>
#include <string.h>

/* Compiler State */

typedef struct {
    char *name;
    int depth;
    bool is_const;
    uint8_t reg;  /* Register holding this local */
} RegLocal;

typedef struct RegFuncContext {
    RegChunk *chunk;
    RegAlloc alloc;

    RegLocal locals[256];
    int local_count;
    int scope_depth;

    struct RegFuncContext *enclosing;
} RegFuncContext;

typedef struct RegCompiler {
    RegFuncContext *current;
    RegChunk *chunk;  /* Main chunk */

    const char *error;
    int error_line;
    bool had_error;
} RegCompiler;

static RegCompiler *compiler = NULL;

/* Error Handling */

static void compile_error(int line, const char *msg) {
    if (!compiler || compiler->had_error) return;
    compiler->had_error = true;
    compiler->error = msg;
    compiler->error_line = line;
    LOG_ERROR("regcompiler: line %d: %s", line, msg);
}

const char *regcompile_error(void) {
    return compiler ? compiler->error : NULL;
}

int regcompile_error_line(void) {
    return compiler ? compiler->error_line : 0;
}

/* Code Generation Helpers */

static RegChunk *current_chunk(void) {
    return compiler->current->chunk;
}

static RegAlloc *current_alloc(void) {
    return &compiler->current->alloc;
}

static void emit(RegInstr instr, int line) {
    regchunk_write(current_chunk(), instr, line);
}

static void emit_op(RegOp op, uint8_t rd, uint8_t rs1, uint8_t rs2, int line) {
    emit(reg_instr(op, rd, rs1, rs2), line);
}

static void emit_imm(RegOp op, uint8_t rd, uint16_t imm, int line) {
    emit(reg_instr_imm(op, rd, imm), line);
}

static size_t emit_jump(RegOp op, uint8_t cond, int line) {
    /* Emit placeholder jump, returns offset to patch */
    size_t offset = current_chunk()->code_size;
    emit(reg_instr_cond_jump(op, cond, 0), line);
    return offset;
}

static void patch_jump(size_t offset) {
    RegChunk *chunk = current_chunk();
    int16_t jump = (int16_t)(chunk->code_size - offset - 1);

    /* Update the jump offset in rs1/rs2 */
    chunk->code[offset].rs1 = (jump >> 8) & 0xFF;
    chunk->code[offset].rs2 = jump & 0xFF;
}

static size_t add_constant(Value *value, int line) {
    size_t idx = regchunk_add_constant(current_chunk(), value);
    if (idx > 0xFFFF) {
        compile_error(line, "too many constants");
        return 0;
    }
    return idx;
}

/* Scope Management */

static void begin_scope(void) {
    compiler->current->scope_depth++;
}

static void end_scope(int line) {
    (void)line;
    compiler->current->scope_depth--;

    /* Remove locals going out of scope */
    while (compiler->current->local_count > 0 &&
           compiler->current->locals[compiler->current->local_count - 1].depth >
               compiler->current->scope_depth) {
        RegLocal *local = &compiler->current->locals[compiler->current->local_count - 1];
        agim_free(local->name);
        compiler->current->local_count--;
    }
}

static int add_local(const char *name, size_t length, bool is_const, int line) {
    if (compiler->current->local_count >= 256) {
        compile_error(line, "too many local variables");
        return -1;
    }

    RegLocal *local = &compiler->current->locals[compiler->current->local_count];
    local->name = agim_alloc(length + 1);
    memcpy(local->name, name, length);
    local->name[length] = '\0';
    local->depth = compiler->current->scope_depth;
    local->is_const = is_const;
    local->reg = regalloc_local(current_alloc(), compiler->current->local_count);

    return compiler->current->local_count++;
}

static int resolve_local(const char *name, size_t length) {
    for (int i = compiler->current->local_count - 1; i >= 0; i--) {
        RegLocal *local = &compiler->current->locals[i];
        if (strlen(local->name) == length &&
            memcmp(local->name, name, length) == 0) {
            return i;
        }
    }
    return -1;
}

/* Forward Declarations */

static void compile_node(AstNode *node);
static uint8_t compile_expr(AstNode *node);
static void compile_stmt(AstNode *node);

/* Expression Compilation */

static uint8_t compile_nil(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    emit_op(ROP_LOAD_NIL, rd, 0, 0, node->line);
    return rd;
}

static uint8_t compile_bool(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    emit_op(node->as.bool_val ? ROP_LOAD_TRUE : ROP_LOAD_FALSE,
            rd, 0, 0, node->line);
    return rd;
}

static uint8_t compile_int(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    int64_t val = node->as.int_val;

    if (val >= -32768 && val <= 32767) {
        emit_imm(ROP_LOAD_INT, rd, (uint16_t)(int16_t)val, node->line);
    } else {
        size_t idx = add_constant(value_int(val), node->line);
        emit_imm(ROP_LOAD_K, rd, (uint16_t)idx, node->line);
    }
    return rd;
}

static uint8_t compile_float(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    size_t idx = add_constant(value_float(node->as.float_val), node->line);
    emit_imm(ROP_LOAD_K, rd, (uint16_t)idx, node->line);
    return rd;
}

static uint8_t compile_string(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    size_t idx = add_constant(value_string(node->as.string_val), node->line);
    emit_imm(ROP_LOAD_K, rd, (uint16_t)idx, node->line);
    return rd;
}

static uint8_t compile_identifier(AstNode *node) {
    const char *name = node->as.ident.name;
    size_t length = strlen(name);

    int slot = resolve_local(name, length);
    if (slot >= 0) {
        /* Local variable - just return its register */
        return compiler->current->locals[slot].reg;
    }

    /* Global variable - load from globals map */
    uint8_t rd = regalloc_new_result(current_alloc());
    size_t idx = add_constant(value_string(name), node->line);
    emit_imm(ROP_GET_GLOBAL, rd, (uint16_t)idx, node->line);
    return rd;
}

static uint8_t compile_binary(AstNode *node) {
    uint8_t left = compile_expr(node->as.binary.left);
    uint8_t right = compile_expr(node->as.binary.right);
    uint8_t rd = regalloc_new_result(current_alloc());

    RegOp op;
    switch (node->as.binary.op) {
    case TOK_PLUS:    op = ROP_ADD; break;
    case TOK_MINUS:   op = ROP_SUB; break;
    case TOK_STAR:    op = ROP_MUL; break;
    case TOK_SLASH:   op = ROP_DIV; break;
    case TOK_PERCENT: op = ROP_MOD; break;
    case TOK_EQ:      op = ROP_EQ; break;
    case TOK_NE:      op = ROP_NE; break;
    case TOK_LT:      op = ROP_LT; break;
    case TOK_LE:      op = ROP_LE; break;
    case TOK_GT:      op = ROP_GT; break;
    case TOK_GE:      op = ROP_GE; break;
    case TOK_AND:     op = ROP_AND; break;
    case TOK_OR:      op = ROP_OR; break;
    default:
        compile_error(node->line, "unknown binary operator");
        return rd;
    }

    emit_op(op, rd, left, right, node->line);

    /* Free temps if they were temporaries */
    if (regalloc_is_temp(current_alloc(), right)) {
        regalloc_free_temp(current_alloc(), right);
    }
    if (regalloc_is_temp(current_alloc(), left)) {
        regalloc_free_temp(current_alloc(), left);
    }

    return rd;
}

static uint8_t compile_unary(AstNode *node) {
    uint8_t operand = compile_expr(node->as.unary.operand);
    uint8_t rd = regalloc_new_result(current_alloc());

    switch (node->as.unary.op) {
    case TOK_MINUS:
        emit_op(ROP_NEG, rd, operand, 0, node->line);
        break;
    case TOK_NOT:
        emit_op(ROP_NOT, rd, operand, 0, node->line);
        break;
    default:
        compile_error(node->line, "unknown unary operator");
    }

    if (regalloc_is_temp(current_alloc(), operand)) {
        regalloc_free_temp(current_alloc(), operand);
    }

    return rd;
}

static uint8_t compile_array(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    emit_op(ROP_ARRAY_NEW, rd, 0, 0, node->line);

    for (size_t i = 0; i < node->as.array.count; i++) {
        uint8_t elem = compile_expr(node->as.array.elements[i]);
        emit_op(ROP_ARRAY_PUSH, rd, elem, 0, node->line);
        if (regalloc_is_temp(current_alloc(), elem)) {
            regalloc_free_temp(current_alloc(), elem);
        }
    }

    return rd;
}

static uint8_t compile_map(AstNode *node) {
    uint8_t rd = regalloc_new_result(current_alloc());
    emit_op(ROP_MAP_NEW, rd, 0, 0, node->line);

    for (size_t i = 0; i < node->as.map.count; i++) {
        /* Keys are strings in the AST */
        size_t key_idx = add_constant(value_string(node->as.map.keys[i]), node->line);
        uint8_t key = regalloc_temp(current_alloc());
        emit_imm(ROP_LOAD_K, key, (uint16_t)key_idx, node->line);

        uint8_t val = compile_expr(node->as.map.values[i]);
        emit_op(ROP_MAP_SET, val, rd, key, node->line);

        if (regalloc_is_temp(current_alloc(), val)) {
            regalloc_free_temp(current_alloc(), val);
        }
        regalloc_free_temp(current_alloc(), key);
    }

    return rd;
}

static uint8_t compile_index(AstNode *node) {
    uint8_t obj = compile_expr(node->as.index_expr.object);
    uint8_t idx = compile_expr(node->as.index_expr.index);
    uint8_t rd = regalloc_new_result(current_alloc());

    /* Use ARRAY_GET for index access (works for both arrays and maps) */
    emit_op(ROP_ARRAY_GET, rd, obj, idx, node->line);

    if (regalloc_is_temp(current_alloc(), idx)) {
        regalloc_free_temp(current_alloc(), idx);
    }
    if (regalloc_is_temp(current_alloc(), obj)) {
        regalloc_free_temp(current_alloc(), obj);
    }

    return rd;
}

static uint8_t compile_member(AstNode *node) {
    uint8_t obj = compile_expr(node->as.member.object);
    uint8_t rd = regalloc_new_result(current_alloc());

    /* Load the field name as a string constant */
    size_t idx = add_constant(value_string(node->as.member.field), node->line);
    uint8_t key = regalloc_temp(current_alloc());
    emit_imm(ROP_LOAD_K, key, (uint16_t)idx, node->line);

    emit_op(ROP_MAP_GET, rd, obj, key, node->line);

    regalloc_free_temp(current_alloc(), key);
    if (regalloc_is_temp(current_alloc(), obj)) {
        regalloc_free_temp(current_alloc(), obj);
    }

    return rd;
}

static uint8_t compile_call(AstNode *node) {
    /* Compile callee */
    uint8_t callee = compile_expr(node->as.call.callee);

    /* Compile arguments */
    uint8_t arg_regs[256];
    size_t arg_count = node->as.call.arg_count;
    if (arg_count > 255) {
        compile_error(node->line, "too many arguments");
        arg_count = 255;
    }

    for (size_t i = 0; i < arg_count; i++) {
        arg_regs[i] = compile_expr(node->as.call.args[i]);
    }

    uint8_t rd = regalloc_new_result(current_alloc());

    /* Emit CALL: rd = call(callee, arg_count) */
    /* First argument register is assumed to follow callee in a contiguous block */
    emit_op(ROP_CALL, rd, callee, (uint8_t)arg_count, node->line);

    /* Free temps */
    for (size_t i = arg_count; i > 0; i--) {
        if (regalloc_is_temp(current_alloc(), arg_regs[i - 1])) {
            regalloc_free_temp(current_alloc(), arg_regs[i - 1]);
        }
    }
    if (regalloc_is_temp(current_alloc(), callee)) {
        regalloc_free_temp(current_alloc(), callee);
    }

    return rd;
}

static uint8_t compile_ternary(AstNode *node) {
    uint8_t cond = compile_expr(node->as.ternary.cond);
    size_t else_jump = emit_jump(ROP_JMP_UNLESS, cond, node->line);

    if (regalloc_is_temp(current_alloc(), cond)) {
        regalloc_free_temp(current_alloc(), cond);
    }

    uint8_t rd = regalloc_new_result(current_alloc());

    /* Then branch */
    uint8_t then_val = compile_expr(node->as.ternary.then_expr);
    emit_op(ROP_MOV, rd, then_val, 0, node->line);
    if (regalloc_is_temp(current_alloc(), then_val)) {
        regalloc_free_temp(current_alloc(), then_val);
    }

    size_t end_jump = emit_jump(ROP_JMP, 0, node->line);
    patch_jump(else_jump);

    /* Else branch */
    uint8_t else_val = compile_expr(node->as.ternary.else_expr);
    emit_op(ROP_MOV, rd, else_val, 0, node->line);
    if (regalloc_is_temp(current_alloc(), else_val)) {
        regalloc_free_temp(current_alloc(), else_val);
    }

    patch_jump(end_jump);

    return rd;
}

static uint8_t compile_expr(AstNode *node) {
    if (!node) {
        uint8_t rd = regalloc_new_result(current_alloc());
        emit_op(ROP_LOAD_NIL, rd, 0, 0, 0);
        return rd;
    }

    switch (node->type) {
    case NODE_NIL:
        return compile_nil(node);
    case NODE_BOOL:
        return compile_bool(node);
    case NODE_INT:
        return compile_int(node);
    case NODE_FLOAT:
        return compile_float(node);
    case NODE_STRING:
        return compile_string(node);
    case NODE_IDENT:
        return compile_identifier(node);
    case NODE_BINARY:
        return compile_binary(node);
    case NODE_UNARY:
        return compile_unary(node);
    case NODE_ARRAY:
        return compile_array(node);
    case NODE_MAP:
        return compile_map(node);
    case NODE_INDEX:
        return compile_index(node);
    case NODE_MEMBER:
        return compile_member(node);
    case NODE_CALL:
        return compile_call(node);
    case NODE_TERNARY:
        return compile_ternary(node);
    default:
        compile_error(node->line, "cannot compile expression");
        return regalloc_new_result(current_alloc());
    }
}

/* Statement Compilation */

static void compile_var_decl(AstNode *node, bool is_const) {
    uint8_t init_reg;
    if (node->as.var_decl.value) {
        init_reg = compile_expr(node->as.var_decl.value);
    } else {
        init_reg = regalloc_temp(current_alloc());
        emit_op(ROP_LOAD_NIL, init_reg, 0, 0, node->line);
    }

    if (compiler->current->scope_depth > 0) {
        /* Local variable */
        int slot = add_local(node->as.var_decl.name, strlen(node->as.var_decl.name),
                            is_const, node->line);
        if (slot >= 0) {
            uint8_t local_reg = compiler->current->locals[slot].reg;
            if (local_reg != init_reg) {
                emit_op(ROP_MOV, local_reg, init_reg, 0, node->line);
            }
        }
    } else {
        /* Global variable */
        size_t idx = add_constant(value_string(node->as.var_decl.name), node->line);
        emit_imm(ROP_SET_GLOBAL, init_reg, (uint16_t)idx, node->line);
    }

    if (regalloc_is_temp(current_alloc(), init_reg)) {
        regalloc_free_temp(current_alloc(), init_reg);
    }
}

static void compile_assignment(AstNode *node) {
    uint8_t val_reg = compile_expr(node->as.assign.value);

    AstNode *target = node->as.assign.target;
    if (target->type == NODE_IDENT) {
        const char *name = target->as.ident.name;
        int slot = resolve_local(name, strlen(name));

        if (slot >= 0) {
            uint8_t local_reg = compiler->current->locals[slot].reg;
            if (local_reg != val_reg) {
                emit_op(ROP_MOV, local_reg, val_reg, 0, node->line);
            }
        } else {
            size_t idx = add_constant(value_string(name), node->line);
            emit_imm(ROP_SET_GLOBAL, val_reg, (uint16_t)idx, node->line);
        }
    } else if (target->type == NODE_INDEX) {
        uint8_t obj = compile_expr(target->as.index_expr.object);
        uint8_t idx = compile_expr(target->as.index_expr.index);
        emit_op(ROP_ARRAY_SET, val_reg, obj, idx, node->line);
        if (regalloc_is_temp(current_alloc(), idx)) {
            regalloc_free_temp(current_alloc(), idx);
        }
        if (regalloc_is_temp(current_alloc(), obj)) {
            regalloc_free_temp(current_alloc(), obj);
        }
    } else if (target->type == NODE_MEMBER) {
        uint8_t obj = compile_expr(target->as.member.object);
        size_t idx = add_constant(value_string(target->as.member.field), node->line);
        uint8_t key = regalloc_temp(current_alloc());
        emit_imm(ROP_LOAD_K, key, (uint16_t)idx, node->line);
        emit_op(ROP_MAP_SET, val_reg, obj, key, node->line);
        regalloc_free_temp(current_alloc(), key);
        if (regalloc_is_temp(current_alloc(), obj)) {
            regalloc_free_temp(current_alloc(), obj);
        }
    }

    if (regalloc_is_temp(current_alloc(), val_reg)) {
        regalloc_free_temp(current_alloc(), val_reg);
    }
}

static void compile_if(AstNode *node) {
    uint8_t cond = compile_expr(node->as.if_stmt.cond);
    size_t else_jump = emit_jump(ROP_JMP_UNLESS, cond, node->line);

    if (regalloc_is_temp(current_alloc(), cond)) {
        regalloc_free_temp(current_alloc(), cond);
    }

    compile_stmt(node->as.if_stmt.then_block);

    if (node->as.if_stmt.else_block) {
        size_t end_jump = emit_jump(ROP_JMP, 0, node->line);
        patch_jump(else_jump);
        compile_stmt(node->as.if_stmt.else_block);
        patch_jump(end_jump);
    } else {
        patch_jump(else_jump);
    }
}

static void compile_while(AstNode *node) {
    size_t loop_start = current_chunk()->code_size;

    uint8_t cond = compile_expr(node->as.while_stmt.cond);
    size_t exit_jump = emit_jump(ROP_JMP_UNLESS, cond, node->line);

    if (regalloc_is_temp(current_alloc(), cond)) {
        regalloc_free_temp(current_alloc(), cond);
    }

    compile_stmt(node->as.while_stmt.body);

    /* Loop back */
    int16_t offset = (int16_t)(loop_start - current_chunk()->code_size - 1);
    emit(reg_instr_cond_jump(ROP_JMP, 0, offset), node->line);

    patch_jump(exit_jump);
}

static void compile_block(AstNode *node) {
    begin_scope();
    for (size_t i = 0; i < node->as.block.count; i++) {
        compile_stmt(node->as.block.stmts[i]);
    }
    end_scope(node->line);
}

static void compile_return(AstNode *node) {
    uint8_t val;
    if (node->as.return_stmt.value) {
        val = compile_expr(node->as.return_stmt.value);
    } else {
        val = regalloc_temp(current_alloc());
        emit_op(ROP_LOAD_NIL, val, 0, 0, node->line);
    }
    emit_op(ROP_RET, val, 0, 0, node->line);
}

static void compile_expr_stmt(AstNode *node) {
    /* NODE_EXPR_STMT wraps an expression */
    /* The statement itself is the wrapper, containing a single child expression */
    /* But looking at the AST, NODE_EXPR_STMT is just a marker - we compile its child */
    /* Actually, looking at AST more carefully - there's no explicit expr_stmt union member */
    /* Let me check: the node itself may be an expression directly used as statement */

    /* For expression statements, we just evaluate and discard */
    uint8_t reg = compile_expr(node);
    if (regalloc_is_temp(current_alloc(), reg)) {
        regalloc_free_temp(current_alloc(), reg);
    }
}

static void compile_stmt(AstNode *node) {
    if (!node) return;

    switch (node->type) {
    case NODE_LET:
        compile_var_decl(node, false);
        break;
    case NODE_CONST:
        compile_var_decl(node, true);
        break;
    case NODE_ASSIGN:
        compile_assignment(node);
        break;
    case NODE_IF:
        compile_if(node);
        break;
    case NODE_WHILE:
        compile_while(node);
        break;
    case NODE_BLOCK:
        compile_block(node);
        break;
    case NODE_RETURN:
        compile_return(node);
        break;
    case NODE_EXPR_STMT:
        compile_expr_stmt(node);
        break;
    default:
        /* Try as expression (for things like function calls used as statements) */
        {
            uint8_t reg = compile_expr(node);
            if (regalloc_is_temp(current_alloc(), reg)) {
                regalloc_free_temp(current_alloc(), reg);
            }
        }
        break;
    }

    /* Free all temps after each statement */
    regalloc_free_all_temps(current_alloc());
}

static void compile_node(AstNode *node) {
    compile_stmt(node);
}

/* Public API */

RegChunk *regcompile(AstNode *ast) {
    if (!ast) return NULL;

    /* Initialize compiler */
    RegCompiler comp;
    comp.had_error = false;
    comp.error = NULL;
    comp.error_line = 0;
    comp.chunk = regchunk_new();

    RegFuncContext main_ctx;
    main_ctx.chunk = comp.chunk;
    regalloc_init(&main_ctx.alloc);
    main_ctx.local_count = 0;
    main_ctx.scope_depth = 0;
    main_ctx.enclosing = NULL;

    comp.current = &main_ctx;
    compiler = &comp;

    /* Compile program */
    if (ast->type == NODE_PROGRAM) {
        for (size_t i = 0; i < ast->as.program.count; i++) {
            compile_node(ast->as.program.decls[i]);
            if (comp.had_error) break;
        }
    } else {
        compile_node(ast);
    }

    /* Add halt at end */
    emit_op(ROP_HALT, 0, 0, 0, 0);

    /* Record register usage */
    comp.chunk->num_regs = regalloc_count(&main_ctx.alloc);

    /* Cleanup locals */
    for (int i = 0; i < main_ctx.local_count; i++) {
        agim_free(main_ctx.locals[i].name);
    }

    compiler = NULL;

    if (comp.had_error) {
        regchunk_free(comp.chunk);
        return NULL;
    }

    return comp.chunk;
}

RegChunk *regcompile_expr(AstNode *ast) {
    if (!ast) return NULL;

    /* Initialize compiler */
    RegCompiler comp;
    comp.had_error = false;
    comp.error = NULL;
    comp.error_line = 0;
    comp.chunk = regchunk_new();

    RegFuncContext main_ctx;
    main_ctx.chunk = comp.chunk;
    regalloc_init(&main_ctx.alloc);
    main_ctx.local_count = 0;
    main_ctx.scope_depth = 0;
    main_ctx.enclosing = NULL;

    comp.current = &main_ctx;
    compiler = &comp;

    /* Compile expression */
    uint8_t result = compile_expr(ast);

    /* Move result to r0 for consistent return value location */
    if (result != 0) {
        emit_op(ROP_MOV, 0, result, 0, ast->line);
    }

    /* Return r0 */
    emit_op(ROP_RET, 0, 0, 0, ast->line);

    /* Record register usage */
    comp.chunk->num_regs = regalloc_count(&main_ctx.alloc);

    compiler = NULL;

    if (comp.had_error) {
        regchunk_free(comp.chunk);
        return NULL;
    }

    return comp.chunk;
}
