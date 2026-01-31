/*
 * Agim - Vector Type Operations
 *
 * Fixed-size floating-point vectors for embeddings and
 * mathematical operations (dot product, cosine similarity, etc.)
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_TYPES_VECTOR_H
#define AGIM_TYPES_VECTOR_H

#include <stdbool.h>
#include <stddef.h>

typedef struct Value Value;

/* Vector Structure */

typedef struct Vector {
    size_t dim;
    double data[];
} Vector;

/* Vector Creation */

Value *value_vector(size_t dim);
Value *value_vector_from(const double *data, size_t dim);

/* Vector Properties */

size_t vector_dim(const Value *v);
const double *vector_data(const Value *v);
double *vector_data_mut(Value *v);
bool value_is_vector(const Value *v);

/* Vector Element Access */

double vector_get(const Value *v, size_t index);
void vector_set(Value *v, size_t index, double val);

/* Vector Math Operations */

double vector_dot(const Value *a, const Value *b);
double vector_magnitude(const Value *v);
Value *vector_normalize(const Value *v);
Value *vector_add(const Value *a, const Value *b);
Value *vector_sub(const Value *a, const Value *b);
Value *vector_scale(const Value *v, double scalar);

/* Vector Similarity/Distance */

double vector_cosine_similarity(const Value *a, const Value *b);
double vector_euclidean_distance(const Value *a, const Value *b);

#endif /* AGIM_TYPES_VECTOR_H */
