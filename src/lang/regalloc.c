/*
 * Agim - Register Allocator Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "lang/regalloc.h"
#include <string.h>

/*============================================================================
 * Initialization
 *============================================================================*/

void regalloc_init(RegAlloc *ra) {
    if (!ra) return;
    regalloc_reset(ra);
}

void regalloc_reset(RegAlloc *ra) {
    if (!ra) return;

    ra->next_reg = 0;
    ra->max_used = 0;
    ra->local_count = 0;
    ra->temp_base = 0;
    ra->temp_count = 0;
    ra->result_reg = REG_NONE;

    memset(ra->local_to_reg, REG_NONE, sizeof(ra->local_to_reg));
}

/*============================================================================
 * Local Variable Allocation
 *============================================================================*/

uint8_t regalloc_local(RegAlloc *ra, int local_slot) {
    if (!ra || local_slot < 0 || local_slot >= REG_MAX) {
        return REG_NONE;
    }

    /* Check if already allocated */
    if (ra->local_to_reg[local_slot] != REG_NONE) {
        return ra->local_to_reg[local_slot];
    }

    /* Allocate new register */
    if (ra->next_reg >= REG_MAX) {
        return REG_NONE; /* Out of registers */
    }

    uint8_t reg = ra->next_reg++;
    ra->local_to_reg[local_slot] = reg;

    if (local_slot >= ra->local_count) {
        ra->local_count = local_slot + 1;
    }

    if (reg > ra->max_used) {
        ra->max_used = reg;
    }

    /* Update temp base to be after locals */
    ra->temp_base = ra->next_reg;

    return reg;
}

uint8_t regalloc_get_local(RegAlloc *ra, int local_slot) {
    if (!ra || local_slot < 0 || local_slot >= REG_MAX) {
        return REG_NONE;
    }
    return ra->local_to_reg[local_slot];
}

/*============================================================================
 * Temporary Register Allocation
 *============================================================================*/

uint8_t regalloc_temp(RegAlloc *ra) {
    if (!ra) return REG_NONE;

    /* Temps start after locals */
    uint8_t reg = ra->temp_base + ra->temp_count;
    if (reg >= REG_MAX) {
        return REG_NONE; /* Out of registers */
    }

    ra->temp_count++;

    if (reg > ra->max_used) {
        ra->max_used = reg;
    }

    return reg;
}

void regalloc_free_temp(RegAlloc *ra, uint8_t reg) {
    if (!ra) return;

    /* Only free if it's the last temp allocated */
    if (ra->temp_count > 0 && reg == ra->temp_base + ra->temp_count - 1) {
        ra->temp_count--;
    }
}

void regalloc_free_all_temps(RegAlloc *ra) {
    if (!ra) return;
    ra->temp_count = 0;
}

/*============================================================================
 * Utilities
 *============================================================================*/

uint8_t regalloc_count(const RegAlloc *ra) {
    if (!ra) return 0;
    return ra->max_used + 1;
}

bool regalloc_is_temp(const RegAlloc *ra, uint8_t reg) {
    if (!ra) return false;
    return reg >= ra->temp_base && reg < ra->temp_base + ra->temp_count;
}

/*============================================================================
 * Expression Result Tracking
 *============================================================================*/

void regalloc_set_result(RegAlloc *ra, uint8_t reg) {
    if (!ra) return;
    ra->result_reg = reg;
}

uint8_t regalloc_get_result(const RegAlloc *ra) {
    if (!ra) return REG_NONE;
    return ra->result_reg;
}

uint8_t regalloc_new_result(RegAlloc *ra) {
    if (!ra) return REG_NONE;

    uint8_t reg = regalloc_temp(ra);
    ra->result_reg = reg;
    return reg;
}
