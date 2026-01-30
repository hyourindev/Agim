/*
 * Agim - Register Allocator
 *
 * Simple linear-scan register allocator for the register-based VM.
 * Maps local variables to virtual registers (0-255).
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#ifndef AGIM_LANG_REGALLOC_H
#define AGIM_LANG_REGALLOC_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/*============================================================================
 * Constants
 *============================================================================*/

#define REG_MAX 256        /* Maximum registers per frame */
#define REG_NONE 255       /* Invalid/no register */

/*============================================================================
 * Register Allocator
 *============================================================================*/

typedef struct RegAlloc {
    uint8_t next_reg;              /* Next available register */
    uint8_t max_used;              /* Highest register used */

    /* Local variable to register mapping */
    uint8_t local_to_reg[REG_MAX]; /* local slot -> register */
    int local_count;

    /* Temporary register pool */
    uint8_t temp_base;             /* First temp register */
    uint8_t temp_count;            /* Number of temps in use */

    /* Reserved registers */
    uint8_t result_reg;            /* Register for expression results */
} RegAlloc;

/*============================================================================
 * Register Allocator API
 *============================================================================*/

/**
 * Initialize a register allocator.
 */
void regalloc_init(RegAlloc *ra);

/**
 * Reset allocator for a new function.
 */
void regalloc_reset(RegAlloc *ra);

/**
 * Allocate a register for a local variable.
 * Returns the allocated register number.
 */
uint8_t regalloc_local(RegAlloc *ra, int local_slot);

/**
 * Get the register for an existing local variable.
 * Returns REG_NONE if not allocated.
 */
uint8_t regalloc_get_local(RegAlloc *ra, int local_slot);

/**
 * Allocate a temporary register.
 * Temps are used for intermediate expression results.
 */
uint8_t regalloc_temp(RegAlloc *ra);

/**
 * Free a temporary register.
 */
void regalloc_free_temp(RegAlloc *ra, uint8_t reg);

/**
 * Free all temporary registers.
 */
void regalloc_free_all_temps(RegAlloc *ra);

/**
 * Get the number of registers used.
 */
uint8_t regalloc_count(const RegAlloc *ra);

/**
 * Check if a register is a temporary.
 */
bool regalloc_is_temp(const RegAlloc *ra, uint8_t reg);

/*============================================================================
 * Expression Result Tracking
 *============================================================================*/

/**
 * Set the result register for the current expression.
 */
void regalloc_set_result(RegAlloc *ra, uint8_t reg);

/**
 * Get the result register.
 */
uint8_t regalloc_get_result(const RegAlloc *ra);

/**
 * Allocate a new result register (temp).
 */
uint8_t regalloc_new_result(RegAlloc *ra);

#endif /* AGIM_LANG_REGALLOC_H */
