/*
 * Signalforge Container Extension
 * src/fast_lookup.c - Swiss Table-inspired fast lookup implementation
 */

#include "../php_signalforge_container.h"
#include "fast_lookup.h"
#include "simd.h"

/* ============================================================================
 * Lifecycle
 * ============================================================================ */

sf_fast_lookup *sf_fast_lookup_create(uint32_t num_groups)
{
    if (num_groups == 0 || num_groups > SF_MAX_GROUPS) {
        num_groups = 4;  /* Default: 64 slots */
    }
    
    sf_fast_lookup *lookup = emalloc(sizeof(sf_fast_lookup));
    lookup->num_groups = num_groups;
    lookup->capacity = num_groups * SF_GROUP_SIZE;
    lookup->count = 0;
    
    /* Allocate groups - use emalloc for consistent allocation/deallocation.
     * SIMD operations can handle unaligned loads on modern CPUs. */
    lookup->groups = emalloc(sizeof(sf_lookup_group) * num_groups);
    
    /* Initialize all control bytes to EMPTY */
    for (uint32_t g = 0; g < num_groups; g++) {
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            lookup->groups[g].ctrl[i] = SF_CTRL_EMPTY;
            lookup->groups[g].keys[i] = NULL;
            ZVAL_UNDEF(&lookup->groups[g].values[i]);
        }
    }
    
    return lookup;
}

void sf_fast_lookup_destroy(sf_fast_lookup *lookup)
{
    if (!lookup) return;
    
    /* Release all stored keys and values 
     * Valid fingerprints are 0x00-0x7F (< SF_CTRL_EMPTY)
     * SF_CTRL_EMPTY (0x80) and SF_CTRL_DELETED (0xFE) are special markers */
    for (uint32_t g = 0; g < lookup->num_groups; g++) {
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            if (lookup->groups[g].ctrl[i] < SF_CTRL_EMPTY) {
                if (lookup->groups[g].keys[i]) {
                    zend_string_release(lookup->groups[g].keys[i]);
                }
                if (!Z_ISUNDEF(lookup->groups[g].values[i])) {
                    zval_ptr_dtor(&lookup->groups[g].values[i]);
                }
            }
        }
    }
    
    efree(lookup->groups);
    
    efree(lookup);
}

void sf_fast_lookup_clear(sf_fast_lookup *lookup)
{
    if (!lookup) return;
    
    /* Release all entries and reset control bytes
     * Valid fingerprints are 0x00-0x7F (< SF_CTRL_EMPTY)
     * Only process slots that actually have data */
    for (uint32_t g = 0; g < lookup->num_groups; g++) {
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            if (lookup->groups[g].ctrl[i] < SF_CTRL_EMPTY) {
                if (lookup->groups[g].keys[i]) {
                    zend_string_release(lookup->groups[g].keys[i]);
                    lookup->groups[g].keys[i] = NULL;
                }
                if (!Z_ISUNDEF(lookup->groups[g].values[i])) {
                    zval_ptr_dtor(&lookup->groups[g].values[i]);
                    ZVAL_UNDEF(&lookup->groups[g].values[i]);
                }
            }
            lookup->groups[g].ctrl[i] = SF_CTRL_EMPTY;
        }
    }
    
    lookup->count = 0;
}

/* ============================================================================
 * Lookup Operations
 * ============================================================================ */

zval *sf_fast_lookup_find(sf_fast_lookup *lookup, zend_string *key)
{
    if (!lookup || !key) return NULL;
    
    zend_ulong h = ZSTR_H(key);
    uint8_t fingerprint = SF_HASH_FINGERPRINT(h);
    uint32_t group_idx = (h >> 7) % lookup->num_groups;
    
#if SF_HAS_SIMD
    /* SIMD path: compare all 16 control bytes at once */
    sf_lookup_group *group = &lookup->groups[group_idx];
    sf_simd_i8x16 ctrl_vec = sf_simd_load_i8x16(group->ctrl);
    sf_simd_i8x16 target = sf_simd_set1_i8(fingerprint);
    sf_simd_i8x16 cmp = sf_simd_cmpeq_i8(ctrl_vec, target);
    
    uint32_t mask = sf_simd_movemask_i8(cmp);
    
    /* Check each matching slot */
    while (mask != 0) {
        int slot = sf_simd_ctz(mask);
        mask &= mask - 1;  /* Clear lowest bit */
        
        if (group->keys[slot] && zend_string_equals(group->keys[slot], key)) {
            return &group->values[slot];
        }
    }
#else
    /* Scalar fallback */
    sf_lookup_group *group = &lookup->groups[group_idx];
    for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
        if (group->ctrl[i] == fingerprint) {
            if (group->keys[i] && zend_string_equals(group->keys[i], key)) {
                return &group->values[i];
            }
        }
    }
#endif
    
    /* Try next group (linear probing) */
    uint32_t probes = 1;
    while (probes < lookup->num_groups) {
        group_idx = (group_idx + 1) % lookup->num_groups;
        
#if SF_HAS_SIMD
        group = &lookup->groups[group_idx];
        ctrl_vec = sf_simd_load_i8x16(group->ctrl);
        cmp = sf_simd_cmpeq_i8(ctrl_vec, target);
        mask = sf_simd_movemask_i8(cmp);
        
        while (mask != 0) {
            int slot = sf_simd_ctz(mask);
            mask &= mask - 1;
            
            if (group->keys[slot] && zend_string_equals(group->keys[slot], key)) {
                return &group->values[slot];
            }
        }
#else
        group = &lookup->groups[group_idx];
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            if (group->ctrl[i] == fingerprint) {
                if (group->keys[i] && zend_string_equals(group->keys[i], key)) {
                    return &group->values[i];
                }
            }
        }
#endif
        
        probes++;
    }
    
    return NULL;
}

int sf_fast_lookup_insert(sf_fast_lookup *lookup, zend_string *key, zval *value)
{
    if (!lookup || !key || !value) return FAILURE;
    
    /* Don't overfill - maintain load factor < 0.875 (14/16) */
    if (lookup->count >= (lookup->capacity * 7 / 8)) {
        return FAILURE;  /* Table too full */
    }
    
    zend_ulong h = ZSTR_H(key);
    uint8_t fingerprint = SF_HASH_FINGERPRINT(h);
    uint32_t group_idx = (h >> 7) % lookup->num_groups;
    
    /* Find first empty or matching slot */
    uint32_t probes = 0;
    while (probes < lookup->num_groups) {
        sf_lookup_group *group = &lookup->groups[group_idx];
        
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            /* Empty or deleted slot - use it */
            if (group->ctrl[i] >= SF_CTRL_DELETED) {
                group->ctrl[i] = fingerprint;
                group->keys[i] = zend_string_copy(key);
                ZVAL_COPY(&group->values[i], value);
                lookup->count++;
                return SUCCESS;
            }
            
            /* Update existing entry */
            if (group->ctrl[i] == fingerprint && group->keys[i] && 
                zend_string_equals(group->keys[i], key)) {
                zval_ptr_dtor(&group->values[i]);
                ZVAL_COPY(&group->values[i], value);
                return SUCCESS;
            }
        }
        
        group_idx = (group_idx + 1) % lookup->num_groups;
        probes++;
    }
    
    return FAILURE;  /* No space found */
}

void sf_fast_lookup_remove(sf_fast_lookup *lookup, zend_string *key)
{
    if (!lookup || !key) return;
    
    zend_ulong h = ZSTR_H(key);
    uint8_t fingerprint = SF_HASH_FINGERPRINT(h);
    uint32_t group_idx = (h >> 7) % lookup->num_groups;
    
    uint32_t probes = 0;
    while (probes < lookup->num_groups) {
        sf_lookup_group *group = &lookup->groups[group_idx];
        
        for (uint32_t i = 0; i < SF_GROUP_SIZE; i++) {
            if (group->ctrl[i] == fingerprint && group->keys[i] && 
                zend_string_equals(group->keys[i], key)) {
                /* Found it - mark as deleted */
                group->ctrl[i] = SF_CTRL_DELETED;
                zend_string_release(group->keys[i]);
                group->keys[i] = NULL;
                zval_ptr_dtor(&group->values[i]);
                ZVAL_UNDEF(&group->values[i]);
                lookup->count--;
                return;
            }
        }
        
        group_idx = (group_idx + 1) % lookup->num_groups;
        probes++;
    }
}
