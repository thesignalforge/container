/*
 * Signalforge Container Extension
 * src/factory.c - Compiled factory operations
 *
 * Factories provide fast-path resolution by pre-computing dependency
 * information and avoiding the full autowiring logic at runtime.
 */

#include "../php_signalforge_container.h"
#include "factory.h"
#include "container.h"

/* ============================================================================
 * Factory Lifecycle
 * ============================================================================ */

sf_factory *sf_factory_create(zend_string *class_name, zend_class_entry *ce)
{
    sf_factory *factory = ecalloc(1, sizeof(sf_factory));
    
    factory->class_name = zend_string_copy(class_name);
    factory->ce = ce;
    factory->factory_fn = NULL;
    factory->dep_names = NULL;
    factory->dep_hashes = NULL;
    factory->dep_count = 0;
    factory->is_singleton = 0;
    factory->has_constructor = (ce && ce->constructor) ? 1 : 0;
    factory->refcount = 1;
    
    return factory;
}

void sf_factory_destroy(sf_factory *factory)
{
    if (!factory) return;
    
    if (factory->class_name) {
        zend_string_release(factory->class_name);
    }
    
    /* Release dependency names */
    if (factory->dep_names) {
        for (uint32_t i = 0; i < factory->dep_count; i++) {
            if (factory->dep_names[i]) {
                zend_string_release(factory->dep_names[i]);
            }
        }
        efree(factory->dep_names);
    }
    
    if (factory->dep_hashes) {
        efree(factory->dep_hashes);
    }
    
    efree(factory);
}

void sf_factory_addref(sf_factory *factory)
{
    if (factory) factory->refcount++;
}

void sf_factory_release(sf_factory *factory)
{
    if (factory && --factory->refcount == 0) {
        sf_factory_destroy(factory);
    }
}

/* ============================================================================
 * Factory Configuration
 * ============================================================================ */

void sf_factory_set_dependencies(sf_factory *factory, zend_string **deps, uint32_t count)
{
    if (!factory) return;
    
    /* Free existing */
    if (factory->dep_names) {
        for (uint32_t i = 0; i < factory->dep_count; i++) {
            if (factory->dep_names[i]) {
                zend_string_release(factory->dep_names[i]);
            }
        }
        efree(factory->dep_names);
        factory->dep_names = NULL;
    }
    if (factory->dep_hashes) {
        efree(factory->dep_hashes);
        factory->dep_hashes = NULL;
    }
    
    factory->dep_count = count;
    
    if (count == 0) return;
    
    /* Allocate and copy */
    factory->dep_names = emalloc(sizeof(zend_string *) * count);
    factory->dep_hashes = emalloc(sizeof(zend_ulong) * count);
    
    for (uint32_t i = 0; i < count; i++) {
        factory->dep_names[i] = zend_string_copy(deps[i]);
        factory->dep_hashes[i] = ZSTR_H(deps[i]);
    }
}

void sf_factory_set_singleton(sf_factory *factory, uint8_t is_singleton)
{
    if (factory) factory->is_singleton = is_singleton;
}

/* ============================================================================
 * Factory Execution
 *
 * This is the fast path - when a factory exists, we call it directly
 * instead of going through the full autowiring logic.
 * ============================================================================ */

int sf_factory_call(sf_factory *factory, sf_container *c, HashTable *params, zval *result)
{
    if (!factory || !factory->factory_fn) {
        return FAILURE;
    }
    
    return factory->factory_fn(c, params, result, factory);
}
