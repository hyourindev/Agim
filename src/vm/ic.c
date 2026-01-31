/*
 * Agim - Inline Cache Implementation
 *
 * Copyright (c) 2025 Agim Language Contributors
 * SPDX-License-Identifier: MIT
 */

#include "vm/ic.h"
#include "vm/value.h"
#include "types/map.h"

#include <stdio.h>
#include <string.h>

/* Initialization */

void ic_init(InlineCache *ic) {
    if (!ic) return;
    ic->state = IC_UNINITIALIZED;
    ic->count = 0;
    memset(ic->entries, 0, sizeof(ic->entries));
}

/* Shape Identification */

uint64_t ic_shape_id(Value *map) {
    if (!map || map->type != VAL_MAP) return 0;
    return (uint64_t)(uintptr_t)map->as.map;
}

/* Cache Lookup */

bool ic_lookup(InlineCache *ic, Value *map, const char *key, Value **result) {
    if (!ic || !map || map->type != VAL_MAP || !key || !result) {
        return false;
    }

    if (ic->state == IC_MEGA || ic->state == IC_UNINITIALIZED) {
        return false;
    }

    uint64_t shape = ic_shape_id(map);
    Map *m = map->as.map;

    /* O(1) direct-mapped cache lookup using shape hash */
    size_t idx = IC_HASH(shape) & IC_CACHE_MASK;
    if (ic->entries[idx].shape_id == shape) {
        size_t bucket = ic->entries[idx].bucket;
        if (bucket < m->capacity) {
            MapEntry *entry = m->buckets[bucket];
            while (entry) {
                if (strcmp(entry->key->data, key) == 0) {
                    *result = entry->value;
                    return true;
                }
                entry = entry->next;
            }
        }
    }

    return false;
}

/* Cache Update */

void ic_update(InlineCache *ic, Value *map, size_t bucket) {
    if (!ic || !map || map->type != VAL_MAP) return;

    if (ic->state == IC_MEGA) return;

    uint64_t shape = ic_shape_id(map);

    /* O(1) direct-mapped cache: hash shape to get slot index */
    size_t idx = IC_HASH(shape) & IC_CACHE_MASK;

    /* Check if this slot already has the same shape */
    if (ic->entries[idx].shape_id == shape) {
        ic->entries[idx].bucket = bucket;
        return;
    }

    /* New shape: track total unique shapes seen */
    ic->count++;

    /* Update cache state based on shapes seen */
    if (ic->count == 1) {
        ic->state = IC_MONO;
    } else if (ic->count <= IC_MAX_ENTRIES) {
        ic->state = IC_POLY;
    } else {
        /* More than IC_MAX_ENTRIES unique shapes = megamorphic */
        ic->state = IC_MEGA;
        return;
    }

    /* Store in direct-mapped slot (may evict previous entry) */
    ic->entries[idx].shape_id = shape;
    ic->entries[idx].bucket = bucket;
}

/* Statistics (Debug) */

#ifdef AGIM_DEBUG
void ic_stats_init(ICStats *stats) {
    if (!stats) return;
    stats->hits = 0;
    stats->misses = 0;
    stats->updates = 0;
    stats->megamorphic_calls = 0;
}

void ic_stats_print(const ICStats *stats) {
    if (!stats) return;
    printf("IC Statistics:\n");
    printf("  Hits:        %zu\n", stats->hits);
    printf("  Misses:      %zu\n", stats->misses);
    printf("  Updates:     %zu\n", stats->updates);
    printf("  Megamorphic: %zu\n", stats->megamorphic_calls);
    if (stats->hits + stats->misses > 0) {
        double hit_rate = 100.0 * stats->hits / (stats->hits + stats->misses);
        printf("  Hit rate:    %.1f%%\n", hit_rate);
    }
}
#endif
