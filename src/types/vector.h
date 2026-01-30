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

/* Forward declaration */
typedef struct Value Value;

/*============================================================================
 * Vector Structure
 *============================================================================*/

/**
 * Fixed-size floating-point vector.
 * Used for embeddings and mathematical operations.
 */
typedef struct Vector {
    size_t dim;         /* Number of dimensions */
    double data[];      /* Flexible array member */
} Vector;

/*============================================================================
 * Vector Creation
 *============================================================================*/

/**
 * Create a new vector value with given dimensions (initialized to zero).
 */
Value *value_vector(size_t dim);

/**
 * Create a vector value from raw data.
 * Data is copied into the vector.
 */
Value *value_vector_from(const double *data, size_t dim);

/*============================================================================
 * Vector Properties
 *============================================================================*/

/**
 * Get the dimension (number of elements) of a vector.
 * Returns 0 if not a vector.
 */
size_t vector_dim(const Value *v);

/**
 * Get pointer to vector data.
 * Returns NULL if not a vector.
 */
const double *vector_data(const Value *v);

/**
 * Get mutable pointer to vector data.
 * Returns NULL if not a vector.
 */
double *vector_data_mut(Value *v);

/**
 * Check if value is a vector.
 */
bool value_is_vector(const Value *v);

/*============================================================================
 * Vector Element Access
 *============================================================================*/

/**
 * Get element at index.
 * Returns 0.0 if out of bounds or not a vector.
 */
double vector_get(const Value *v, size_t index);

/**
 * Set element at index.
 * No-op if out of bounds or not a vector.
 */
void vector_set(Value *v, size_t index, double val);

/*============================================================================
 * Vector Math Operations
 *============================================================================*/

/**
 * Compute dot product of two vectors.
 * Returns 0.0 if vectors have different dimensions or are not vectors.
 */
double vector_dot(const Value *a, const Value *b);

/**
 * Compute magnitude (L2 norm) of a vector.
 * Returns 0.0 if not a vector.
 */
double vector_magnitude(const Value *v);

/**
 * Create a normalized copy of the vector (unit vector).
 * Returns nil if not a vector or magnitude is zero.
 */
Value *vector_normalize(const Value *v);

/**
 * Add two vectors element-wise.
 * Returns nil if dimensions don't match or not vectors.
 */
Value *vector_add(const Value *a, const Value *b);

/**
 * Subtract two vectors element-wise (a - b).
 * Returns nil if dimensions don't match or not vectors.
 */
Value *vector_sub(const Value *a, const Value *b);

/**
 * Scale vector by a scalar.
 * Returns nil if not a vector.
 */
Value *vector_scale(const Value *v, double scalar);

/*============================================================================
 * Vector Similarity/Distance
 *============================================================================*/

/**
 * Compute cosine similarity between two vectors.
 * Returns value in [-1, 1], or 0.0 on error.
 */
double vector_cosine_similarity(const Value *a, const Value *b);

/**
 * Compute Euclidean distance between two vectors.
 * Returns 0.0 if dimensions don't match or not vectors.
 */
double vector_euclidean_distance(const Value *a, const Value *b);

#endif /* AGIM_TYPES_VECTOR_H */
