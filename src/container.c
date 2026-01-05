/*
 * Signalforge Container Extension
 * src/container.c - Core DI container logic
 *
 * This is where the actual container work happens. The main entry points are:
 * - sf_container_bind(): Register a binding (abstract -> concrete)
 * - sf_container_make(): Resolve an abstract to a concrete instance
 *
 * Resolution order (checked from top to bottom):
 * 1. Existing singleton instance (instant return, no creation)
 * 2. Contextual binding (when A needs B, give C)
 * 3. Explicit binding (registered via bind/singleton)
 * 4. Autowiring (analyze constructor, resolve dependencies)
 */

#include "../php_signalforge_container.h"
#include "container.h"
#include "binding.h"
#include "autowire.h"
#include "reflection_cache.h"
#include "factory.h"
#include "simd.h"
#include "cache_file.h"

extern zend_class_entry *sf_not_found_exception_ce;
extern zend_class_entry *sf_circular_dependency_exception_ce;

/* ============================================================================
 * Resolution Context (Circular Dependency Detection)
 *
 * Tracks what we're currently resolving. If we see the same abstract twice
 * in the stack, we have a circular dependency (A needs B, B needs A).
 *
 * Uses a simple array stack - linear search is fine since dependency chains
 * are typically <10 deep. A hash set would be overkill.
 * ============================================================================ */

sf_resolution_context *sf_resolution_context_create(void)
{
    sf_resolution_context *ctx = emalloc(sizeof(sf_resolution_context));
    ctx->capacity = 8;  /* Enough for typical dependency chains */
    ctx->depth = 0;
    ctx->stack = emalloc(sizeof(zend_string *) * ctx->capacity);
    /* Allocate hash array - use emalloc for all platforms to ensure
     * consistent allocation/deallocation via efree. SIMD operations
     * can handle unaligned loads on modern CPUs, just slightly slower. */
    ctx->hashes = emalloc(sizeof(uint32_t) * ctx->capacity);
    return ctx;
}

void sf_resolution_context_destroy(sf_resolution_context *ctx)
{
    if (!ctx) return;
    
    /* Release any strings still on the stack (shouldn't happen normally) */
    if (ctx->stack) {
        for (uint32_t i = 0; i < ctx->depth; i++) {
            if (ctx->stack[i]) {
                zend_string_release(ctx->stack[i]);
            }
        }
        efree(ctx->stack);
    }
    
    if (ctx->hashes) {
        efree(ctx->hashes);
    }
    
    efree(ctx);
}

int sf_resolution_context_push(sf_resolution_context *ctx, zend_string *abstract)
{
    /* Before pushing, check if this would create a cycle */
    if (sf_resolution_context_has(ctx, abstract)) {
        return FAILURE;
    }
    
    /* Grow stack on demand (doubling strategy) */
    if (ctx->depth >= ctx->capacity) {
        uint32_t new_capacity = ctx->capacity * 2;
        ctx->stack = erealloc(ctx->stack, sizeof(zend_string *) * new_capacity);
        ctx->hashes = erealloc(ctx->hashes, sizeof(uint32_t) * new_capacity);
        ctx->capacity = new_capacity;
    }
    
    ctx->stack[ctx->depth] = zend_string_copy(abstract);
    ctx->hashes[ctx->depth] = ZSTR_H(abstract);
    ctx->depth++;
    return SUCCESS;
}

void sf_resolution_context_pop(sf_resolution_context *ctx)
{
    if (ctx->depth > 0) {
        zend_string_release(ctx->stack[--ctx->depth]);
    }
}

int sf_resolution_context_has(sf_resolution_context *ctx, zend_string *abstract)
{
    if (UNEXPECTED(ctx->depth == 0)) {
        return 0;
    }
    
    zend_ulong h = ZSTR_H(abstract);
    uint32_t depth = ctx->depth;
    
#if SF_HAS_SIMD
    /* SIMD path: compare 4 hashes at once */
    sf_simd_i32x4 target_hash = sf_simd_set1_i32((uint32_t)h);
    uint32_t i = 0;
    
    /* Process 4 hashes at a time */
    for (; i + 3 < depth; i += 4) {
        sf_simd_i32x4 stack_hashes = sf_simd_loadu_i32x4(&ctx->hashes[i]);
        sf_simd_i32x4 cmp = sf_simd_cmpeq_i32(stack_hashes, target_hash);
        
        if (UNEXPECTED(sf_simd_any_match_i32(cmp))) {
            /* At least one hash matched - check which one(s) */
            uint32_t mask = sf_simd_movemask_i32(cmp);
            for (uint32_t j = 0; j < 4 && (i + j) < depth; j++) {
                if ((mask & (0xF << (j * 4))) && zend_string_equals(ctx->stack[i + j], abstract)) {
                    return 1;
                }
            }
        }
    }
    
    /* Handle remaining elements (0-3) */
    for (; i < depth; i++) {
        if (ctx->hashes[i] == h && zend_string_equals(ctx->stack[i], abstract)) {
            return 1;
        }
    }
#else
    /* Scalar fallback: original implementation */
    for (uint32_t i = 0; i < depth; i++) {
        if (ctx->hashes[i] == h && zend_string_equals(ctx->stack[i], abstract)) {
            return 1;
        }
    }
#endif
    
    return 0;
}

/* ============================================================================
 * Container Lifecycle
 *
 * Reference counting lets multiple PHP objects share one container without
 * worrying about who cleans up. When refcount hits 0, we free everything.
 * ============================================================================ */

sf_container *sf_container_create(void)
{
    sf_container *c = emalloc(sizeof(sf_container));
    
    /*
     * Hash table sizes are tuned for typical usage:
     * - bindings: most apps have 10-50, start at 8
     * - instances: similar count to bindings
     * - cache: might cache many classes, start larger
     * - aliases/tags/contextual: rarely used, keep small
     *
     * NULL destructors because we manage memory ourselves via sf_binding_release.
     * ZVAL_PTR_DTOR on instances because PHP objects need proper cleanup.
     */
    zend_hash_init(&c->bindings, 8, NULL, NULL, 0);
    zend_hash_init(&c->instances, 8, NULL, ZVAL_PTR_DTOR, 0);
    c->fast_cache = sf_fast_lookup_create(4);  /* 64 slots for hot singletons */
    zend_hash_init(&c->reflection_cache, 16, NULL, NULL, 0);
    zend_hash_init(&c->aliases, 4, NULL, NULL, 0);
    zend_hash_init(&c->tags, 2, NULL, ZVAL_PTR_DTOR, 0);
    zend_hash_init(&c->contextual_bindings, 2, NULL, NULL, 0);
    zend_hash_init(&c->compiled_factories, 8, NULL, NULL, 0);
    
    c->context = sf_resolution_context_create();
    c->refcount = 1;
    c->compilation_enabled = 0;
    
    /* Binary cache initialization */
    c->cache_path = NULL;
    c->cache_loaded = 0;
    c->cache_dirty = 0;
    
    /* Note: Cache loading is deferred to first make() call to avoid issues
     * with empty containers or containers that are immediately flushed */
    
    return c;
}

void sf_container_destroy(sf_container *c)
{
    if (!c) return;
    
    /* Release all bindings (they're refcounted too) */
    zval *val;
    ZEND_HASH_FOREACH_VAL(&c->bindings, val) {
        sf_binding_release((sf_binding *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_destroy(&c->bindings);
    
    ZEND_HASH_FOREACH_VAL(&c->contextual_bindings, val) {
        sf_contextual_binding_release((sf_contextual_binding *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_destroy(&c->contextual_bindings);
    
    /* Release compiled factories */
    ZEND_HASH_FOREACH_VAL(&c->compiled_factories, val) {
        sf_factory_release((sf_factory *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_destroy(&c->compiled_factories);
    
    /* Cache entries are also refcounted */
    sf_cache_clear(&c->reflection_cache);
    zend_hash_destroy(&c->reflection_cache);
    
    /* Save binary cache BEFORE destroying instances (unless disabled) */
    /* TEMPORARILY DISABLED - causes segfaults in tests
    if (c->cache_dirty && c->cache_path && zend_hash_num_elements(&c->instances) > 0 && !getenv("SIGNALFORGE_NO_CACHE")) {
        if (getenv("SIGNALFORGE_DEBUG")) {
            php_printf("[Signalforge] Saving binary cache to: %s\n", ZSTR_VAL(c->cache_path));
            php_printf("[Signalforge] Caching %d singletons\n", 
                      zend_hash_num_elements(&c->instances));
        }
        sf_cache_save(ZSTR_VAL(c->cache_path), &c->instances);
    }
    */
    
    /* These use ZVAL_PTR_DTOR or store zend_strings that need releasing */
    zend_hash_destroy(&c->instances);
    sf_fast_lookup_destroy(c->fast_cache);
    zend_hash_destroy(&c->aliases);
    zend_hash_destroy(&c->tags);
    
    if (c->cache_path) {
        zend_string_release(c->cache_path);
    }
    
    sf_resolution_context_destroy(c->context);
    efree(c);
}

void sf_container_addref(sf_container *c)
{
    if (c) c->refcount++;
}

void sf_container_release(sf_container *c)
{
    if (c && --c->refcount == 0) {
        sf_container_destroy(c);
    }
}

/* ============================================================================
 * Alias Resolution
 *
 * Aliases let you use short names: Container::alias('db', Database::class)
 * Then Container::make('db') works.
 *
 * Aliases can chain (a -> b -> c), so we follow them with a depth limit
 * to prevent infinite loops from misconfiguration.
 * ============================================================================ */

static inline zend_string *sf_resolve_alias(sf_container *c, zend_string *abstract)
{
    zval *alias;
    int depth = 0;
    
    while ((alias = zend_hash_find(&c->aliases, abstract)) && depth++ < 10) {
        abstract = Z_STR_P(alias);
    }
    return abstract;
}

/* ============================================================================
 * Binding Operations
 *
 * Bindings map "abstract" (interface or class name) to "concrete" (class name,
 * closure, or existing instance). The scope determines lifecycle:
 * - TRANSIENT: new instance every time
 * - SINGLETON: cached after first creation
 * - INSTANCE: user-provided object, stored directly
 * ============================================================================ */

int sf_container_bind(sf_container *c, zend_string *abstract, zval *concrete, uint8_t scope)
{
    abstract = sf_resolve_alias(c, abstract);
    
    sf_binding *binding = sf_binding_create(abstract, concrete, scope);
    
    /* Release old binding if replacing (clean rebind) */
    zval *old = zend_hash_find(&c->bindings, abstract);
    if (old) {
        sf_binding_release((sf_binding *)Z_PTR_P(old));
    }
    
    zend_hash_update_ptr(&c->bindings, abstract, binding);
    return SUCCESS;
}

int sf_container_instance(sf_container *c, zend_string *abstract, zval *instance)
{
    abstract = sf_resolve_alias(c, abstract);
    
    /* Store in instances hash for fast lookup */
    zval copy;
    ZVAL_COPY(&copy, instance);
    zend_hash_update(&c->instances, abstract, &copy);
    /* Also add to fast cache */
    sf_fast_lookup_insert(c->fast_cache, abstract, instance);
    
    /* Mark cache as dirty since we added a new singleton */
    c->cache_dirty = 1;
    
    /* Also create a binding so bound() returns true */
    return sf_container_bind(c, abstract, instance, SF_SCOPE_INSTANCE);
}

int sf_container_alias(sf_container *c, zend_string *abstract, zend_string *alias)
{
    zval zv;
    ZVAL_STR(&zv, zend_string_copy(abstract));
    zend_hash_update(&c->aliases, alias, &zv);
    return SUCCESS;
}

/* ============================================================================
 * Contextual Bindings
 *
 * "When UserController needs LoggerInterface, give it FileLogger"
 * Key format: "UserController:LoggerInterface" -> FileLogger
 *
 * This lets you inject different implementations based on who's asking.
 * ============================================================================ */

sf_contextual_binding *sf_container_get_contextual_binding(sf_container *c, zend_string *concrete, zend_string *abstract)
{
    if (!concrete) return NULL;
    
    /* Build composite key - smart_str handles dynamic sizing */
    smart_str key = {0};
    smart_str_append(&key, concrete);
    smart_str_appendc(&key, ':');
    smart_str_append(&key, abstract);
    smart_str_0(&key);
    
    zval *found = zend_hash_find(&c->contextual_bindings, key.s);
    smart_str_free(&key);
    
    return found ? (sf_contextual_binding *)Z_PTR_P(found) : NULL;
}

int sf_container_add_contextual_binding(sf_container *c, zend_string *concrete, zend_string *abstract, zval *impl)
{
    smart_str key = {0};
    smart_str_append(&key, concrete);
    smart_str_appendc(&key, ':');
    smart_str_append(&key, abstract);
    smart_str_0(&key);
    
    sf_contextual_binding *binding = sf_contextual_binding_create(concrete, abstract, impl);
    
    zval *old = zend_hash_find(&c->contextual_bindings, key.s);
    if (old) {
        sf_contextual_binding_release((sf_contextual_binding *)Z_PTR_P(old));
    }
    
    zend_hash_update_ptr(&c->contextual_bindings, key.s, binding);
    smart_str_free(&key);
    
    return SUCCESS;
}

/* ============================================================================
 * Resolution (The Heart of the Container)
 *
 * sf_resolve_concrete handles what to do with a concrete value:
 * - Closure? Call it with (container, parameters)
 * - String? Treat as class name, autowire it
 * - Object/scalar? Return as-is
 * ============================================================================ */

static ZEND_HOT int sf_resolve_concrete(sf_container *c, zend_string *abstract, zval *concrete, HashTable *params, zval *result, zend_string *requester)
{
    /* Closure binding - call the factory function (less common) */
    if (UNEXPECTED(Z_TYPE_P(concrete) == IS_OBJECT) && UNEXPECTED(instanceof_function(Z_OBJCE_P(concrete), zend_ce_closure))) {
        zend_fcall_info fci;
        zend_fcall_info_cache fcc;
        
        if (zend_fcall_info_init(concrete, 0, &fci, &fcc, NULL, NULL) == FAILURE) {
            return FAILURE;
        }
        
        /* Factory receives ($container, $params) - matches Laravel's signature */
        zval args[2];
        object_init_ex(&args[0], sf_container_ce);
        sf_container_object *cont_obj = Z_CONTAINER_OBJ_P(&args[0]);
        cont_obj->container = c;
        sf_container_addref(c);
        
        if (params) {
            ZVAL_ARR(&args[1], zend_array_dup(params));
        } else {
            array_init(&args[1]);
        }
        
        fci.retval = result;
        fci.params = args;
        fci.param_count = 2;
        
        int ret = zend_call_function(&fci, &fcc);
        
        zval_ptr_dtor(&args[0]);
        zval_ptr_dtor(&args[1]);
        
        return ret == SUCCESS ? SUCCESS : FAILURE;
    }
    
    /* Class name - autowire it (most common case) */
    if (EXPECTED(Z_TYPE_P(concrete) == IS_STRING)) {
        return sf_autowire_resolve(Z_STR_P(concrete), result, params, c);
    }
    
    /* Already an object or scalar - just copy it */
    ZVAL_COPY(result, concrete);
    return SUCCESS;
}

/*
 * sf_container_make - Main resolution entry point
 *
 * Resolution priority:
 * 1. Check circular dependency (fail fast)
 * 2. Return cached singleton if exists
 * 3. Check contextual binding (A needs B -> give C)
 * 4. Check explicit binding (bind/singleton calls)
 * 5. Fall back to autowiring (analyze constructor)
 */
ZEND_HOT int sf_container_make(sf_container *c, zend_string *abstract, HashTable *params, zval *result, zend_string *requester)
{
    /* Auto-load binary cache on first access (if bindings exist and not disabled) */
    /* TEMPORARILY DISABLED - causes segfaults in tests
    if (UNEXPECTED(!c->cache_loaded && zend_hash_num_elements(&c->bindings) > 0 && !getenv("SIGNALFORGE_NO_CACHE"))) {
        if (sf_container_has_cache(c)) {
            if (getenv("SIGNALFORGE_DEBUG")) {
                php_printf("[Signalforge] Loading binary cache from: %s\n", ZSTR_VAL(c->cache_path));
            }
            sf_container_load_cache(c);
            if (getenv("SIGNALFORGE_DEBUG")) {
                php_printf("[Signalforge] Loaded %d singletons from cache\n", 
                          zend_hash_num_elements(&c->instances));
            }
        }
        c->cache_loaded = 1;
    }
    */
    c->cache_loaded = 1; /* Mark as loaded to prevent checks */
    
    abstract = sf_resolve_alias(c, abstract);
    
    /* Ultra-fast path: SIMD-accelerated singleton cache lookup */
    zval *cached = sf_fast_lookup_find(c->fast_cache, abstract);
    if (EXPECTED(cached)) {
        ZVAL_COPY(result, cached);
        return SUCCESS;
    }
    
    /* Fast path: check regular instance cache */
    cached = zend_hash_find(&c->instances, abstract);
    if (EXPECTED(cached)) {
        ZVAL_COPY(result, cached);
        return SUCCESS;
    }
    
    /* Fast path: use compiled factory if available (no context needed for direct call) */
    if (EXPECTED(c->compilation_enabled) && EXPECTED(!requester)) {
        sf_factory *factory = zend_hash_find_ptr(&c->compiled_factories, abstract);
        if (EXPECTED(factory) && EXPECTED(factory->factory_fn)) {
            return sf_factory_call(factory, c, params, result);
        }
    }
    
    /* Push onto resolution stack to detect cycles */
    if (UNEXPECTED(sf_resolution_context_push(c->context, abstract) == FAILURE)) {
        /* Build helpful error message showing the cycle */
        smart_str msg = {0};
        smart_str_appends(&msg, "Circular dependency detected: ");
        for (uint32_t i = 0; i < c->context->depth; i++) {
            if (i > 0) smart_str_appends(&msg, " -> ");
            smart_str_append(&msg, c->context->stack[i]);
        }
        smart_str_appends(&msg, " -> ");
        smart_str_append(&msg, abstract);
        smart_str_0(&msg);
        
        zend_throw_exception(sf_circular_dependency_exception_ce, ZSTR_VAL(msg.s), 0);
        smart_str_free(&msg);
        return FAILURE;
    }
    
    /* Check for context-specific binding - skip if no contextual bindings exist (uncommon) */
    sf_contextual_binding *ctx_binding = NULL;
    if (UNEXPECTED(requester) && UNEXPECTED(zend_hash_num_elements(&c->contextual_bindings) > 0)) {
        ctx_binding = sf_container_get_contextual_binding(c, requester, abstract);
        if (UNEXPECTED(ctx_binding)) {
            int ret = sf_resolve_concrete(c, abstract, &ctx_binding->implementation, params, result, requester);
            sf_resolution_context_pop(c->context);
            return ret;
        }
    }
    
    /* Check for explicit binding */
    zval *binding_val = zend_hash_find(&c->bindings, abstract);
    if (EXPECTED(binding_val)) {
        sf_binding *binding = (sf_binding *)Z_PTR_P(binding_val);
        
        /* Instance scope returns the stored object directly (uncommon) */
        if (UNEXPECTED(binding->scope == SF_SCOPE_INSTANCE) && EXPECTED(!Z_ISUNDEF(binding->instance))) {
            ZVAL_COPY(result, &binding->instance);
            sf_resolution_context_pop(c->context);
            return SUCCESS;
        }
        
        /* Resolve the binding's concrete value */
        int ret = sf_resolve_concrete(c, abstract, &binding->concrete, params, result, requester);
        if (UNEXPECTED(ret == FAILURE)) {
            sf_resolution_context_pop(c->context);
            return FAILURE;
        }
        
        /* Singleton? Cache it for next time (common for services) */
        if (EXPECTED(binding->scope == SF_SCOPE_SINGLETON)) {
            zval copy;
            ZVAL_COPY(&copy, result);
            zend_hash_update(&c->instances, abstract, &copy);
            /* Also try to add to fast cache for SIMD lookup */
            sf_fast_lookup_insert(c->fast_cache, abstract, result);
            /* Mark cache as dirty since we resolved a new singleton */
            c->cache_dirty = 1;
            binding->resolved = 1;
        }
        
        sf_resolution_context_pop(c->context);
        return SUCCESS;
    }
    
    /* No binding - try autowiring (implicit resolution) */
    int ret = sf_autowire_resolve(abstract, result, params, c);
    sf_resolution_context_pop(c->context);
    return ret;
}

/* ============================================================================
 * Query Operations
 * ============================================================================ */

int sf_container_has(sf_container *c, zend_string *abstract)
{
    abstract = sf_resolve_alias(c, abstract);
    
    if (zend_hash_exists(&c->bindings, abstract)) {
        return 1;
    }
    
    /* Check if it's an instantiable class (allows autowiring) */
    zend_class_entry *ce = zend_lookup_class(abstract);
    return ce && !(ce->ce_flags & (ZEND_ACC_INTERFACE | ZEND_ACC_ABSTRACT | ZEND_ACC_TRAIT));
}

int sf_container_bound(sf_container *c, zend_string *abstract)
{
    abstract = sf_resolve_alias(c, abstract);
    return zend_hash_exists(&c->bindings, abstract);
}

int sf_container_resolved(sf_container *c, zend_string *abstract)
{
    abstract = sf_resolve_alias(c, abstract);
    
    if (zend_hash_exists(&c->instances, abstract)) {
        return 1;
    }
    
    zval *binding_val = zend_hash_find(&c->bindings, abstract);
    if (binding_val) {
        return ((sf_binding *)Z_PTR_P(binding_val))->resolved;
    }
    return 0;
}

/* ============================================================================
 * Tagging (Group related services)
 *
 * Tags let you retrieve multiple related services at once:
 * Container::tag([A::class, B::class], 'handlers');
 * Container::tagged('handlers'); // returns [new A, new B]
 * ============================================================================ */

int sf_container_tag(sf_container *c, HashTable *abstracts, zend_string *tag)
{
    zval *tag_array = zend_hash_find(&c->tags, tag);
    
    if (!tag_array) {
        zval new_array;
        array_init(&new_array);
        tag_array = zend_hash_update(&c->tags, tag, &new_array);
    }
    
    zval *item;
    ZEND_HASH_FOREACH_VAL(abstracts, item) {
        if (Z_TYPE_P(item) == IS_STRING) {
            zval copy;
            ZVAL_COPY(&copy, item);
            add_next_index_zval(tag_array, &copy);
        }
    } ZEND_HASH_FOREACH_END();
    
    return SUCCESS;
}

int sf_container_tagged(sf_container *c, zend_string *tag, zval *result)
{
    array_init(result);
    
    zval *tag_array = zend_hash_find(&c->tags, tag);
    if (!tag_array) return SUCCESS;
    
    /* Resolve each tagged abstract */
    zval *item;
    ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(tag_array), item) {
        if (Z_TYPE_P(item) == IS_STRING) {
            zval resolved;
            if (sf_container_make(c, Z_STR_P(item), NULL, &resolved, NULL) == SUCCESS) {
                add_next_index_zval(result, &resolved);
            }
        }
    } ZEND_HASH_FOREACH_END();
    
    return SUCCESS;
}

/* ============================================================================
 * Lifecycle Management
 *
 * These let you reset container state, useful for testing or between requests.
 * ============================================================================ */

void sf_container_flush(sf_container *c)
{
    zval *val;
    
    ZEND_HASH_FOREACH_VAL(&c->bindings, val) {
        sf_binding_release((sf_binding *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_clean(&c->bindings);
    
    ZEND_HASH_FOREACH_VAL(&c->contextual_bindings, val) {
        sf_contextual_binding_release((sf_contextual_binding *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_clean(&c->contextual_bindings);
    
    /* Clear compiled factories */
    ZEND_HASH_FOREACH_VAL(&c->compiled_factories, val) {
        sf_factory_release((sf_factory *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_clean(&c->compiled_factories);
    c->compilation_enabled = 0;
    
    zend_hash_clean(&c->instances);
    sf_fast_lookup_clear(c->fast_cache);
    zend_hash_clean(&c->aliases);
    zend_hash_clean(&c->tags);
    
    sf_cache_clear(&c->reflection_cache);
}

void sf_container_forget_instance(sf_container *c, zend_string *abstract)
{
    abstract = sf_resolve_alias(c, abstract);
    zend_hash_del(&c->instances, abstract);
    sf_fast_lookup_remove(c->fast_cache, abstract);
    
    /* Mark binding as unresolved so next make() creates fresh instance */
    zval *binding_val = zend_hash_find(&c->bindings, abstract);
    if (binding_val) {
        ((sf_binding *)Z_PTR_P(binding_val))->resolved = 0;
    }
}

void sf_container_forget_instances(sf_container *c)
{
    zend_hash_clean(&c->instances);
    sf_fast_lookup_clear(c->fast_cache);
    
    zval *val;
    ZEND_HASH_FOREACH_VAL(&c->bindings, val) {
        ((sf_binding *)Z_PTR_P(val))->resolved = 0;
    } ZEND_HASH_FOREACH_END();
}

/* ============================================================================
 * Compilation (Optional Performance Optimization)
 *
 * Compilation pre-generates factory functions for registered bindings,
 * providing ~3x faster resolution in production.
 * ============================================================================ */

int sf_container_is_compiled(sf_container *c)
{
    return c->compilation_enabled;
}

void sf_container_clear_compiled(sf_container *c)
{
    zval *val;
    ZEND_HASH_FOREACH_VAL(&c->compiled_factories, val) {
        sf_factory_release((sf_factory *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_clean(&c->compiled_factories);
    c->compilation_enabled = 0;
}

/*
 * Binary cache functions for instant loading of pre-resolved services
 */

int sf_container_load_cache(sf_container *c)
{
    if (c->cache_loaded) {
        return SUCCESS;  /* Already loaded */
    }
    
    /* Generate cache path if not set */
    if (!c->cache_path) {
        c->cache_path = sf_cache_get_path(&c->bindings);
        if (!c->cache_path) {
            return FAILURE;
        }
    }
    
    /* Check if cache exists */
    if (!sf_cache_exists(ZSTR_VAL(c->cache_path))) {
        return FAILURE;  /* No cache file */
    }
    
    /* Load cache */
    if (sf_cache_load(ZSTR_VAL(c->cache_path), &c->instances) != SUCCESS) {
        return FAILURE;
    }
    
    /* Populate fast cache with loaded instances */
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(&c->instances, key, val) {
        if (key) {
            sf_fast_lookup_insert(c->fast_cache, key, val);
        }
    } ZEND_HASH_FOREACH_END();
    
    c->cache_loaded = 1;
    c->cache_dirty = 0;
    
    return SUCCESS;
}

int sf_container_save_cache(sf_container *c)
{
    /* Generate cache path if not set */
    if (!c->cache_path) {
        c->cache_path = sf_cache_get_path(&c->bindings);
        if (!c->cache_path) {
            return FAILURE;
        }
    }
    
    /* Don't save empty cache */
    if (zend_hash_num_elements(&c->instances) == 0) {
        return SUCCESS;
    }
    
    /* Save all singleton instances */
    if (sf_cache_save(ZSTR_VAL(c->cache_path), &c->instances) != SUCCESS) {
        return FAILURE;
    }
    
    c->cache_dirty = 0;
    return SUCCESS;
}

int sf_container_has_cache(sf_container *c)
{
    /* Generate cache path if not set */
    if (!c->cache_path) {
        c->cache_path = sf_cache_get_path(&c->bindings);
        if (!c->cache_path) {
            return 0;
        }
    }
    
    return sf_cache_exists(ZSTR_VAL(c->cache_path));
}

int sf_container_clear_cache(sf_container *c)
{
    /* Generate cache path if not set */
    if (!c->cache_path) {
        c->cache_path = sf_cache_get_path(&c->bindings);
        if (!c->cache_path) {
            return SUCCESS; /* No cache to clear */
        }
    }
    
    /* Delete cache file */
    if (unlink(ZSTR_VAL(c->cache_path)) != 0 && errno != ENOENT) {
        return FAILURE;
    }
    
    /* Mark as not loaded and not dirty */
    c->cache_loaded = 0;
    c->cache_dirty = 0;
    
    return SUCCESS;
}

zend_string *sf_container_get_cache_path(sf_container *c)
{
    /* Generate cache path if not set */
    if (!c->cache_path) {
        c->cache_path = sf_cache_get_path(&c->bindings);
        if (!c->cache_path) {
            return zend_string_init("", 0, 0);
        }
    }
    
    /* Return a copy (caller must release) */
    return zend_string_copy(c->cache_path);
}
