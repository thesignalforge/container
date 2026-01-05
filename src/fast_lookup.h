/*
 * Signalforge Container Extension
 * src/fast_lookup.h - Swiss Table-inspired fast lookup for hot paths
 *
 * Implements a SIMD-accelerated lookup structure for frequently accessed
 * singleton instances. Uses control bytes (hash fingerprints) for quick
 * filtering before full key comparison.
 */

#ifndef SF_FAST_LOOKUP_H
#define SF_FAST_LOOKUP_H

#include "php.h"
#include "simd.h"

/* Group size matches SIMD register width (16 bytes) */
#define SF_GROUP_SIZE 16

/* Maximum number of groups in fast lookup (256 entries total) */
#define SF_MAX_GROUPS 16

/* Control byte states */
#define SF_CTRL_EMPTY    0x80  /* Slot is empty */
#define SF_CTRL_DELETED  0xFE  /* Slot was deleted */
#define SF_CTRL_SENTINEL 0xFF  /* End marker */

/* Extract 7-bit hash fingerprint for control byte */
#define SF_HASH_FINGERPRINT(h) ((uint8_t)((h) & 0x7F))

/* A group of 16 slots with control bytes */
typedef struct {
    uint8_t ctrl[SF_GROUP_SIZE] __attribute__((aligned(16)));  /* Control bytes */
    zend_string *keys[SF_GROUP_SIZE];                          /* Key pointers */
    zval values[SF_GROUP_SIZE];                                /* Cached values */
} sf_lookup_group;

/* Fast lookup table for hot-path singleton cache */
typedef struct {
    sf_lookup_group *groups;     /* Array of groups */
    uint32_t num_groups;         /* Number of allocated groups */
    uint32_t count;              /* Number of entries */
    uint32_t capacity;           /* Total capacity (num_groups * SF_GROUP_SIZE) */
} sf_fast_lookup;

/* Initialize fast lookup table */
sf_fast_lookup *sf_fast_lookup_create(uint32_t num_groups);

/* Destroy fast lookup table */
void sf_fast_lookup_destroy(sf_fast_lookup *lookup);

/* Find an entry (returns NULL if not found) */
zval *sf_fast_lookup_find(sf_fast_lookup *lookup, zend_string *key);

/* Insert or update an entry (returns SUCCESS/FAILURE) */
int sf_fast_lookup_insert(sf_fast_lookup *lookup, zend_string *key, zval *value);

/* Remove an entry */
void sf_fast_lookup_remove(sf_fast_lookup *lookup, zend_string *key);

/* Clear all entries */
void sf_fast_lookup_clear(sf_fast_lookup *lookup);

#endif /* SF_FAST_LOOKUP_H */
