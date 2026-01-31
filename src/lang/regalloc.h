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

#define REG_MAX 256
#define REG_NONE 255

/* Register Allocator */

typedef struct RegAlloc {
    uint8_t next_reg;
    uint8_t max_used;
    uint8_t local_to_reg[REG_MAX];
    int local_count;
    uint8_t temp_base;
    uint8_t temp_count;
    uint8_t result_reg;
} RegAlloc;

/* Register Allocator API */

void regalloc_init(RegAlloc *ra);
void regalloc_reset(RegAlloc *ra);
uint8_t regalloc_local(RegAlloc *ra, int local_slot);
uint8_t regalloc_get_local(RegAlloc *ra, int local_slot);
uint8_t regalloc_temp(RegAlloc *ra);
void regalloc_free_temp(RegAlloc *ra, uint8_t reg);
void regalloc_free_all_temps(RegAlloc *ra);
uint8_t regalloc_count(const RegAlloc *ra);
bool regalloc_is_temp(const RegAlloc *ra, uint8_t reg);

/* Expression Result Tracking */

void regalloc_set_result(RegAlloc *ra, uint8_t reg);
uint8_t regalloc_get_result(const RegAlloc *ra);
uint8_t regalloc_new_result(RegAlloc *ra);

#endif /* AGIM_LANG_REGALLOC_H */
