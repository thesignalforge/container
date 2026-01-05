/*
 * Signalforge Container Extension
 * src/container.h - Core container interface
 */

#ifndef SF_CONTAINER_H
#define SF_CONTAINER_H

#include "binding.h"
#include "reflection_cache.h"
#include "fast_lookup.h"

/* Tracks what's being resolved to detect circular dependencies (A->B->A) */
struct _sf_resolution_context {
    zend_string **stack;  /* Array of abstracts currently being resolved */
    uint32_t *hashes;     /* Pre-computed hashes for SIMD comparison (aligned) */
    uint32_t depth;       /* Current stack depth */
    uint32_t capacity;    /* Allocated size (grows on demand) */
} __attribute__((aligned(16)));

/* The main container - holds all bindings, instances, and caches
 * Layout optimized for cache line alignment (hot fields in first cache line) */
struct _sf_container {
    /* Hot fields (accessed on every make() call) - first cache line (64 bytes) */
    sf_fast_lookup *fast_cache;      /* SIMD-accelerated singleton cache for hot paths */
    HashTable instances;             /* abstract => zval (cached singletons) */
    HashTable bindings;              /* abstract => sf_binding* */
    sf_resolution_context *context;  /* Current resolution stack */
    
    /* Warm fields (accessed frequently but not every call) - second cache line */
    HashTable reflection_cache;      /* class_name => sf_class_meta* */
    HashTable compiled_factories;    /* class_name => sf_factory* (compiled mode) */
    zend_bool compilation_enabled;   /* Flag for compilation mode */
    uint32_t refcount;               /* Reference counting for safe sharing */
    
    /* Binary cache fields */
    zend_string *cache_path;         /* Path to binary cache file */
    zend_bool cache_loaded;          /* Whether cache has been loaded */
    zend_bool cache_dirty;           /* Whether cache needs to be saved */
    
    /* Cold fields (rarely accessed) - third cache line */
    HashTable contextual_bindings;   /* "concrete:abstract" => sf_contextual_binding* */
    HashTable aliases;               /* alias => abstract */
    HashTable tags;                  /* tag => array of abstracts */
} __attribute__((aligned(64)));

/* Container lifecycle */
sf_container *sf_container_create(void);
void sf_container_destroy(sf_container *container);
void sf_container_addref(sf_container *container);
void sf_container_release(sf_container *container);

/* Binding registration */
int sf_container_bind(sf_container *container, zend_string *abstract, zval *concrete, uint8_t scope);
int sf_container_instance(sf_container *container, zend_string *abstract, zval *instance);
int sf_container_alias(sf_container *container, zend_string *abstract, zend_string *alias);

/* Resolution */
int sf_container_make(sf_container *container, zend_string *abstract, HashTable *parameters, zval *result, zend_string *requesting_class);
int sf_container_has(sf_container *container, zend_string *abstract);
int sf_container_bound(sf_container *container, zend_string *abstract);
int sf_container_resolved(sf_container *container, zend_string *abstract);

/* Contextual bindings (when X needs Y, give Z) */
int sf_container_add_contextual_binding(sf_container *container, zend_string *concrete, zend_string *abstract, zval *implementation);
sf_contextual_binding *sf_container_get_contextual_binding(sf_container *container, zend_string *concrete, zend_string *abstract);

/* Tagging (group related services) */
int sf_container_tag(sf_container *container, HashTable *abstracts, zend_string *tag);
int sf_container_tagged(sf_container *container, zend_string *tag, zval *result);

/* State management */
void sf_container_flush(sf_container *container);
void sf_container_forget_instance(sf_container *container, zend_string *abstract);
void sf_container_forget_instances(sf_container *container);

/* Compilation (optional performance optimization) */
int sf_container_compile(sf_container *container);
int sf_container_is_compiled(sf_container *container);
void sf_container_clear_compiled(sf_container *container);

/* Binary cache (automatic performance optimization) */
int sf_container_load_cache(sf_container *container);
int sf_container_save_cache(sf_container *container);
int sf_container_has_cache(sf_container *container);
int sf_container_clear_cache(sf_container *container);
zend_string *sf_container_get_cache_path(sf_container *container);

/* Resolution context (internal) */
sf_resolution_context *sf_resolution_context_create(void);
void sf_resolution_context_destroy(sf_resolution_context *context);
int sf_resolution_context_push(sf_resolution_context *context, zend_string *abstract);
void sf_resolution_context_pop(sf_resolution_context *context);
int sf_resolution_context_has(sf_resolution_context *context, zend_string *abstract);

#endif /* SF_CONTAINER_H */
