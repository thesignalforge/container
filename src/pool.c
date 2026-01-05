/*
 * Signalforge Container Extension
 * src/pool.c - Object pooling implementation
 */

#include "../php_signalforge_container.h"
#include "pool.h"

/* Thread-local storage for pool manager */
#ifdef ZTS
    static TSRMLS_D sf_pool_manager *sf_tls_pool = NULL;
#else
    static sf_pool_manager sf_global_pool = {0};
#endif

/* ============================================================================
 * Pool Initialization
 * ============================================================================ */

static void sf_buffer_pool_init(sf_buffer_pool *pool, uint32_t capacity, uint32_t buffer_size)
{
    pool->capacity = capacity;
    pool->buffer_size = buffer_size;
    pool->buffers = emalloc(sizeof(zval *) * capacity);
    pool->in_use = ecalloc(capacity, sizeof(uint8_t));
    
    /* Pre-allocate all buffers */
    for (uint32_t i = 0; i < capacity; i++) {
        pool->buffers[i] = emalloc(sizeof(zval) * buffer_size);
    }
}

static void sf_buffer_pool_destroy(sf_buffer_pool *pool)
{
    if (!pool->buffers) return;
    
    for (uint32_t i = 0; i < pool->capacity; i++) {
        efree(pool->buffers[i]);
    }
    efree(pool->buffers);
    efree(pool->in_use);
    
    pool->buffers = NULL;
    pool->in_use = NULL;
}

void sf_pool_init(sf_pool_manager *mgr)
{
    if (mgr->initialized) return;
    
    sf_buffer_pool_init(&mgr->pool_8, SF_POOL_SIZE_8, 8);
    sf_buffer_pool_init(&mgr->pool_16, SF_POOL_SIZE_16, 16);
    sf_buffer_pool_init(&mgr->pool_32, SF_POOL_SIZE_32, 32);
    
    mgr->initialized = 1;
}

void sf_pool_destroy(sf_pool_manager *mgr)
{
    if (!mgr->initialized) return;
    
    sf_buffer_pool_destroy(&mgr->pool_8);
    sf_buffer_pool_destroy(&mgr->pool_16);
    sf_buffer_pool_destroy(&mgr->pool_32);
    
    mgr->initialized = 0;
}

/* ============================================================================
 * Pool Operations
 * ============================================================================ */

static zval *sf_buffer_pool_acquire(sf_buffer_pool *pool)
{
    /* Find first available buffer */
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (!pool->in_use[i]) {
            pool->in_use[i] = 1;
            return pool->buffers[i];
        }
    }
    return NULL;  /* Pool exhausted */
}

static void sf_buffer_pool_release(sf_buffer_pool *pool, zval *buffer)
{
    /* Find which buffer this is and mark as available */
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (pool->buffers[i] == buffer) {
            pool->in_use[i] = 0;
            return;
        }
    }
}

zval *sf_pool_acquire(sf_pool_manager *mgr, uint32_t size)
{
    if (!mgr->initialized) {
        sf_pool_init(mgr);
    }
    
    /* Select appropriate pool based on size */
    if (size <= 8) {
        return sf_buffer_pool_acquire(&mgr->pool_8);
    } else if (size <= 16) {
        return sf_buffer_pool_acquire(&mgr->pool_16);
    } else if (size <= 32) {
        return sf_buffer_pool_acquire(&mgr->pool_32);
    }
    
    /* Size too large for pooling */
    return NULL;
}

void sf_pool_release(sf_pool_manager *mgr, zval *buffer, uint32_t size)
{
    if (!mgr->initialized || !buffer) return;
    
    /* Return to appropriate pool */
    if (size <= 8) {
        sf_buffer_pool_release(&mgr->pool_8, buffer);
    } else if (size <= 16) {
        sf_buffer_pool_release(&mgr->pool_16, buffer);
    } else if (size <= 32) {
        sf_buffer_pool_release(&mgr->pool_32, buffer);
    }
}

/* ============================================================================
 * Thread-Local Access
 * ============================================================================ */

sf_pool_manager *sf_pool_get_manager(void)
{
#ifdef ZTS
    /* In ZTS mode, each thread gets its own pool */
    if (!sf_tls_pool) {
        sf_tls_pool = emalloc(sizeof(sf_pool_manager));
        memset(sf_tls_pool, 0, sizeof(sf_pool_manager));
        sf_pool_init(sf_tls_pool);
    }
    return sf_tls_pool;
#else
    /* Non-ZTS: single global pool */
    if (!sf_global_pool.initialized) {
        sf_pool_init(&sf_global_pool);
    }
    return &sf_global_pool;
#endif
}
