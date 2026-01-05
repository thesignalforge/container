/*
 * Signalforge Container Extension
 * src/reflection_cache.c - Reflection metadata caching
 *
 * PHP's Reflection API is powerful but slow - creating a ReflectionClass and
 * iterating its parameters takes time. Since constructor signatures don't
 * change at runtime, we cache the metadata in native structs.
 *
 * First resolution of a class: ~microseconds (builds metadata)
 * Subsequent resolutions: ~nanoseconds (hash lookup)
 *
 * The cache is per-container, so it gets cleaned up when the container is
 * destroyed at request end.
 */

#include "../php_signalforge_container.h"
#include "reflection_cache.h"

/* 
 * Create array of param_info structs.
 * Using ecalloc zeros memory, so we don't need explicit initialization.
 */
sf_param_info *sf_param_info_create(uint32_t count)
{
    return count ? ecalloc(count, sizeof(sf_param_info)) : NULL;
}

void sf_param_info_destroy(sf_param_info *params, uint32_t count)
{
    if (!params) return;
    
    for (uint32_t i = 0; i < count; i++) {
        if (params[i].name) {
            zend_string_release(params[i].name);
        }
        if (params[i].type_hint) {
            zend_string_release(params[i].type_hint);
        }
        if (!Z_ISUNDEF(params[i].default_value)) {
            zval_ptr_dtor(&params[i].default_value);
        }
    }
    efree(params);
}

/* ============================================================================
 * Class Metadata (Reference Counted)
 *
 * We use reference counting so the same metadata can be in the cache AND
 * being used by autowiring simultaneously without copying or dangling pointers.
 * ============================================================================ */

sf_class_meta *sf_class_meta_create(zend_string *class_name)
{
    sf_class_meta *meta = emalloc(sizeof(sf_class_meta));
    
    meta->class_name = zend_string_copy(class_name);
    meta->param_count = 0;
    meta->params = NULL;
    meta->is_instantiable = 1;
    meta->refcount = 1;
    
    return meta;
}

void sf_class_meta_destroy(sf_class_meta *meta)
{
    if (!meta) return;
    
    zend_string_release(meta->class_name);
    if (meta->params) {
        sf_param_info_destroy(meta->params, meta->param_count);
    }
    efree(meta);
}

void sf_class_meta_addref(sf_class_meta *meta)
{
    if (meta) meta->refcount++;
}

void sf_class_meta_release(sf_class_meta *meta)
{
    if (meta && --meta->refcount == 0) {
        sf_class_meta_destroy(meta);
    }
}

/*
 * Build metadata by inspecting the class's constructor.
 *
 * We use Zend's internal structures directly (zend_function, zend_arg_info)
 * instead of PHP's Reflection classes. This is faster and avoids userland
 * object creation overhead.
 */
sf_class_meta *sf_cache_build(zend_string *class_name, zend_class_entry *ce)
{
    if (!ce) return NULL;
    
    sf_class_meta *meta = sf_class_meta_create(class_name);
    
    /* Interfaces, abstract classes, and traits can't be instantiated */
    if (ce->ce_flags & (ZEND_ACC_INTERFACE | ZEND_ACC_ABSTRACT | ZEND_ACC_TRAIT)) {
        meta->is_instantiable = 0;
        return meta;
    }
    
    /* No constructor? No parameters to worry about */
    zend_function *ctor = ce->constructor;
    if (!ctor) return meta;
    
    uint32_t num_args = ctor->common.num_args;
    uint32_t required = ctor->common.required_num_args;
    
    if (num_args == 0) return meta;
    
    meta->param_count = num_args;
    meta->params = sf_param_info_create(num_args);
    
    /*
     * Walk through each parameter and extract:
     * - Name (for matching user-provided params)
     * - Type hint class name (for container resolution)
     * - Whether nullable or optional
     */
    for (uint32_t i = 0; i < num_args; i++) {
        zend_arg_info *arg = &ctor->common.arg_info[i];
        sf_param_info *p = &meta->params[i];
        
        p->name = zend_string_copy(arg->name);
        
        /* Extract class type hint if present */
        if (ZEND_TYPE_IS_SET(arg->type)) {
            if (ZEND_TYPE_HAS_NAME(arg->type)) {
                p->type_hint = zend_string_copy(ZEND_TYPE_NAME(arg->type));
            }
            p->is_nullable = ZEND_TYPE_ALLOW_NULL(arg->type);
        }
        
        /* Optional parameters (those beyond required_num_args) have defaults */
        p->has_default = (i >= required);
        p->is_variadic = ZEND_ARG_IS_VARIADIC(arg);
        
        /*
         * For default values: we mark has_default=true but leave default_value
         * as UNDEF. The autowire code will handle this by not passing arguments
         * for trailing optional parameters, allowing PHP to use its own defaults.
         * 
         * This is cleaner than trying to extract defaults from op_array internals
         * which varies between PHP versions and involves parsing opcodes.
         */
        ZVAL_UNDEF(&p->default_value);
    }
    
    return meta;
}

/* ============================================================================
 * Cache Operations
 *
 * The cache is just a hash table mapping class name -> metadata.
 * Metadata lifetime is bound to container - callers borrow without refcount.
 * ============================================================================ */

sf_class_meta *sf_cache_get(zend_string *class_name, HashTable *cache)
{
    zval *cached = zend_hash_find(cache, class_name);
    if (cached) {
        return (sf_class_meta *)Z_PTR_P(cached);
    }
    return NULL;
}

void sf_cache_put(zend_string *class_name, sf_class_meta *meta, HashTable *cache)
{
    if (!meta) return;
    
    sf_class_meta_addref(meta);  /* Cache holds a reference */
    zend_hash_add_ptr(cache, class_name, meta);
}

void sf_cache_clear(HashTable *cache)
{
    zval *val;
    ZEND_HASH_FOREACH_VAL(cache, val) {
        sf_class_meta_release((sf_class_meta *)Z_PTR_P(val));
    } ZEND_HASH_FOREACH_END();
    zend_hash_clean(cache);
}
