/*
 * Signalforge Container Extension
 * src/pool.h - Object pooling for zval argument buffers
 *
 * Reduces malloc/free overhead during autowiring by reusing buffers.
 * Uses thread-local storage for ZTS safety.
 */

#ifndef SF_POOL_H
#define SF_POOL_H

#include "php.h"

/* Pool sizes for common argument counts */
#define SF_POOL_SIZE_8   4   /* Pool 4 buffers of size 8 */
#define SF_POOL_SIZE_16  2   /* Pool 2 buffers of size 16 */
#define SF_POOL_SIZE_32  1   /* Pool 1 buffer of size 32 */

/* Buffer pool for a specific size */
typedef struct {
    zval **buffers;      /* Array of buffer pointers */
    uint8_t *in_use;     /* Bitmap of which buffers are in use */
    uint32_t capacity;   /* Number of buffers in pool */
    uint32_t buffer_size;/* Size of each buffer (in zvals) */
} sf_buffer_pool;

/* Global pool manager (thread-local in ZTS) */
typedef struct {
    sf_buffer_pool pool_8;   /* For 1-8 arguments */
    sf_buffer_pool pool_16;  /* For 9-16 arguments */
    sf_buffer_pool pool_32;  /* For 17-32 arguments */
    zend_bool initialized;
} sf_pool_manager;

/* Initialize the pool manager (called once per thread) */
void sf_pool_init(sf_pool_manager *mgr);

/* Destroy the pool manager (called on thread shutdown) */
void sf_pool_destroy(sf_pool_manager *mgr);

/* Acquire a buffer from the pool (returns NULL if pool exhausted) */
zval *sf_pool_acquire(sf_pool_manager *mgr, uint32_t size);

/* Release a buffer back to the pool */
void sf_pool_release(sf_pool_manager *mgr, zval *buffer, uint32_t size);

/* Get the thread-local pool manager */
sf_pool_manager *sf_pool_get_manager(void);

#endif /* SF_POOL_H */
