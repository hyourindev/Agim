/*
 * Agim - Vector Type Operations
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "types/vector.h"
#include "vm/value.h"
#include "util/alloc.h"

#include <math.h>
#include <stdatomic.h>
#include <string.h>

/* Vector Creation */

Value *value_vector(size_t dim) {
    if (dim == 0) return value_nil();

    Value *v = agim_alloc(sizeof(Value));
    v->type = VAL_VECTOR;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = VALUE_IMMUTABLE;
    v->gc_state = 0;
    v->next = NULL;

    Vector *vec = agim_alloc(sizeof(Vector) + sizeof(double) * dim);
    vec->dim = dim;
    memset(vec->data, 0, sizeof(double) * dim);

    v->as.vector = vec;
    return v;
}

Value *value_vector_from(const double *data, size_t dim) {
    if (!data || dim == 0) return value_nil();

    Value *v = agim_alloc(sizeof(Value));
    v->type = VAL_VECTOR;
    atomic_store_explicit(&v->refcount, 1, memory_order_relaxed);
    v->flags = VALUE_IMMUTABLE;
    v->gc_state = 0;
    v->next = NULL;

    Vector *vec = agim_alloc(sizeof(Vector) + sizeof(double) * dim);
    vec->dim = dim;
    memcpy(vec->data, data, sizeof(double) * dim);

    v->as.vector = vec;
    return v;
}

/* Vector Properties */

size_t vector_dim(const Value *v) {
    if (!v || v->type != VAL_VECTOR) return 0;
    Vector *vec = (Vector *)v->as.vector;
    return vec->dim;
}

const double *vector_data(const Value *v) {
    if (!v || v->type != VAL_VECTOR) return NULL;
    Vector *vec = (Vector *)v->as.vector;
    return vec->data;
}

double *vector_data_mut(Value *v) {
    if (!v || v->type != VAL_VECTOR) return NULL;
    Vector *vec = (Vector *)v->as.vector;
    return vec->data;
}

bool value_is_vector(const Value *v) {
    return v && v->type == VAL_VECTOR;
}

/* Vector Element Access */

double vector_get(const Value *v, size_t index) {
    if (!v || v->type != VAL_VECTOR) return 0.0;
    Vector *vec = (Vector *)v->as.vector;
    if (index >= vec->dim) return 0.0;
    return vec->data[index];
}

void vector_set(Value *v, size_t index, double val) {
    if (!v || v->type != VAL_VECTOR) return;
    Vector *vec = (Vector *)v->as.vector;
    if (index >= vec->dim) return;
    vec->data[index] = val;
}

/* Vector Math Operations */

double vector_dot(const Value *a, const Value *b) {
    if (!a || a->type != VAL_VECTOR || !b || b->type != VAL_VECTOR) {
        return 0.0;
    }

    Vector *va = (Vector *)a->as.vector;
    Vector *vb = (Vector *)b->as.vector;

    if (va->dim != vb->dim) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < va->dim; i++) {
        sum += va->data[i] * vb->data[i];
    }
    return sum;
}

double vector_magnitude(const Value *v) {
    if (!v || v->type != VAL_VECTOR) return 0.0;

    Vector *vec = (Vector *)v->as.vector;
    double sum = 0.0;

    for (size_t i = 0; i < vec->dim; i++) {
        sum += vec->data[i] * vec->data[i];
    }

    return sqrt(sum);
}

Value *vector_normalize(const Value *v) {
    if (!v || v->type != VAL_VECTOR) return value_nil();

    double mag = vector_magnitude(v);
    if (mag == 0.0) return value_nil();

    Vector *vec = (Vector *)v->as.vector;
    Value *result = value_vector(vec->dim);
    Vector *res_vec = (Vector *)result->as.vector;

    for (size_t i = 0; i < vec->dim; i++) {
        res_vec->data[i] = vec->data[i] / mag;
    }

    return result;
}

Value *vector_add(const Value *a, const Value *b) {
    if (!a || a->type != VAL_VECTOR || !b || b->type != VAL_VECTOR) {
        return value_nil();
    }

    Vector *va = (Vector *)a->as.vector;
    Vector *vb = (Vector *)b->as.vector;

    if (va->dim != vb->dim) return value_nil();

    Value *result = value_vector(va->dim);
    Vector *res_vec = (Vector *)result->as.vector;

    for (size_t i = 0; i < va->dim; i++) {
        res_vec->data[i] = va->data[i] + vb->data[i];
    }

    return result;
}

Value *vector_sub(const Value *a, const Value *b) {
    if (!a || a->type != VAL_VECTOR || !b || b->type != VAL_VECTOR) {
        return value_nil();
    }

    Vector *va = (Vector *)a->as.vector;
    Vector *vb = (Vector *)b->as.vector;

    if (va->dim != vb->dim) return value_nil();

    Value *result = value_vector(va->dim);
    Vector *res_vec = (Vector *)result->as.vector;

    for (size_t i = 0; i < va->dim; i++) {
        res_vec->data[i] = va->data[i] - vb->data[i];
    }

    return result;
}

Value *vector_scale(const Value *v, double scalar) {
    if (!v || v->type != VAL_VECTOR) return value_nil();

    Vector *vec = (Vector *)v->as.vector;
    Value *result = value_vector(vec->dim);
    Vector *res_vec = (Vector *)result->as.vector;

    for (size_t i = 0; i < vec->dim; i++) {
        res_vec->data[i] = vec->data[i] * scalar;
    }

    return result;
}

/* Vector Similarity/Distance */

double vector_cosine_similarity(const Value *a, const Value *b) {
    if (!a || a->type != VAL_VECTOR || !b || b->type != VAL_VECTOR) {
        return 0.0;
    }

    double dot = vector_dot(a, b);
    double mag_a = vector_magnitude(a);
    double mag_b = vector_magnitude(b);

    if (mag_a == 0.0 || mag_b == 0.0) return 0.0;

    return dot / (mag_a * mag_b);
}

double vector_euclidean_distance(const Value *a, const Value *b) {
    if (!a || a->type != VAL_VECTOR || !b || b->type != VAL_VECTOR) {
        return 0.0;
    }

    Vector *va = (Vector *)a->as.vector;
    Vector *vb = (Vector *)b->as.vector;

    if (va->dim != vb->dim) return 0.0;

    double sum = 0.0;
    for (size_t i = 0; i < va->dim; i++) {
        double diff = va->data[i] - vb->data[i];
        sum += diff * diff;
    }

    return sqrt(sum);
}
