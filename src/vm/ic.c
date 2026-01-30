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

/*============================================================================
 * Inline Cache Initialization
 *============================================================================*/

void ic_init(InlineCache *ic) {
    if (!ic) return;
    ic->state = IC_UNINITIALIZED;
    ic->count = 0;
    memset(ic->entries, 0, sizeof(ic->entries));
}

/*============================================================================
 * Shape Identification
 *============================================================================*/

uint64_t ic_shape_id(Value *map) {
    if (!map || map->type != VAL_MAP) return 0;

    /*
     * Use the map's internal pointer as the shape ID.
     * This is simple but effective: different maps have different addresses,
     * and the same map maintains its address until COW triggers a copy.
     *
     * For more sophisticated shape tracking, we could:
     * - Use a shape/hidden class system
     * - Hash the key set of the map
     * - Use a monotonically increasing shape counter
     */
    return (uint64_t)(uintptr_t)map->as.map;
}

/*============================================================================
 * Cache Lookup
 *============================================================================*/

bool ic_lookup(InlineCache *ic, Value *map, const char *key, Value **result) {
    if (!ic || !map || map->type != VAL_MAP || !key || !result) {
        return false;
    }

    /* Megamorphic: always miss, use slow path */
    if (ic->state == IC_MEGA) {
        return false;
    }

    /* Uninitialized: miss */
    if (ic->state == IC_UNINITIALIZED) {
        return false;
    }

    uint64_t shape = ic_shape_id(map);
    Map *m = map->as.map;

    /* Search cache entries */
    for (uint8_t i = 0; i < ic->count; i++) {
        if (ic->entries[i].shape_id == shape) {
            /* Cache hit: verify the bucket still contains our key */
            size_t bucket = ic->entries[i].bucket;
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
            /* Key not found at cached bucket (map mutated?) - miss */
            return false;
        }
    }

    /* Shape not in cache - miss */
    return false;
}

/*============================================================================
 * Cache Update
 *============================================================================*/

void ic_update(InlineCache *ic, Value *map, size_t bucket) {
    if (!ic || !map || map->type != VAL_MAP) return;

    /* Don't update if already megamorphic */
    if (ic->state == IC_MEGA) return;

    uint64_t shape = ic_shape_id(map);

    /* Check if shape already exists in cache */
    for (uint8_t i = 0; i < ic->count; i++) {
        if (ic->entries[i].shape_id == shape) {
            /* Update bucket for existing shape */
            ic->entries[i].bucket = bucket;
            return;
        }
    }

    /* New shape - add to cache */
    if (ic->count < IC_MAX_ENTRIES) {
        ic->entries[ic->count].shape_id = shape;
        ic->entries[ic->count].bucket = bucket;
        ic->count++;

        /* Update state */
        if (ic->count == 1) {
            ic->state = IC_MONO;
        } else {
            ic->state = IC_POLY;
        }
    } else {
        /* Too many shapes - go megamorphic */
        ic->state = IC_MEGA;
    }
}

/*============================================================================
 * Statistics (Debug)
 *============================================================================*/

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
