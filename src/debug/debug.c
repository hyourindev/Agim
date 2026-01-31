/*
 * Agim - Debug Utilities
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "vm/bytecode.h"
#include "vm/value.h"
#include "vm/vm.h"

#include <stdio.h>

/* Debug utilities are implemented in bytecode.c and vm.c */
/* This file is reserved for additional debug tooling */

void debug_print_value_detailed(const Value *v) {
    if (!v) {
        printf("(null)\n");
        return;
    }

    printf("Value {\n");
    printf("  type: ");

    switch (v->type) {
    case VAL_NIL:
        printf("nil\n");
        break;
    case VAL_BOOL:
        printf("bool = %s\n", v->as.boolean ? "true" : "false");
        break;
    case VAL_INT:
        printf("int = %ld\n", v->as.integer);
        break;
    case VAL_FLOAT:
        printf("float = %g\n", v->as.floating);
        break;
    case VAL_STRING:
        printf("string = \"%s\" (len=%zu, hash=%zu)\n",
               v->as.string->data,
               v->as.string->length,
               v->as.string->hash);
        break;
    case VAL_ARRAY:
        printf("array (len=%zu, cap=%zu)\n",
               v->as.array->length,
               v->as.array->capacity);
        break;
    case VAL_MAP:
        printf("map (size=%zu, cap=%zu)\n",
               v->as.map->size,
               v->as.map->capacity);
        break;
    case VAL_PID:
        printf("pid = %lu\n", v->as.pid);
        break;
    case VAL_FUNCTION:
        printf("function = %s (arity=%zu)\n",
               v->as.function->name ? v->as.function->name : "<anonymous>",
               v->as.function->arity);
        break;
    case VAL_BYTES:
        printf("bytes (len=%zu, cap=%zu)\n",
               v->as.bytes->length,
               v->as.bytes->capacity);
        break;
    case VAL_VECTOR: {
        Vector *vec = (Vector *)v->as.vector;
        printf("vector (dim=%zu)\n", vec->dim);
        break;
    }
    case VAL_CLOSURE:
        printf("closure\n");
        break;
    case VAL_RESULT:
        printf("result\n");
        break;
    case VAL_OPTION:
        printf("option\n");
        break;
    case VAL_STRUCT:
        printf("struct\n");
        break;
    case VAL_ENUM:
        printf("enum\n");
        break;
    }

    printf("  marked: %s\n", value_is_marked(v) ? "yes" : "no");
    printf("}\n");
}
