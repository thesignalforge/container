/*
 * Signalforge Container Extension
 * src/reflection_cache.h - Reflection metadata caching
 */

#ifndef SF_REFLECTION_CACHE_H
#define SF_REFLECTION_CACHE_H

/* Metadata about a single constructor parameter
 * Layout optimized for cache efficiency (hot fields first) */
struct _sf_param_info {
    /* Hot fields (accessed during autowiring) */
    zend_string *type_hint;   /* Class/interface name (NULL for scalars) */
    zend_string *name;        /* Parameter name (for matching user params) */
    zval default_value;       /* The default value */
    
    /* Cold fields (flags checked less frequently) */
    zend_bool is_nullable;    /* Accepts null? */
    zend_bool has_default;    /* Has default value? */
    zend_bool is_variadic;    /* Is ...$param? */
    uint8_t _padding[5];      /* Align to 8 bytes */
} __attribute__((aligned(8)));

/* Cached constructor metadata for a class
 * Layout optimized for cache line alignment (hot fields first) */
struct _sf_class_meta {
    /* Hot fields (accessed during every autowire) - first cache line */
    zend_string *class_name;    /* FQCN */
    sf_param_info *params;      /* Array of parameter metadata */
    uint32_t param_count;       /* Number of constructor params */
    zend_bool is_instantiable;  /* Can we new this? (not interface/abstract) */
    
    /* Cold fields */
    uint32_t refcount;
    uint8_t _padding[3];        /* Align to 8 bytes */
} __attribute__((aligned(64)));

/* Cache operations */
sf_class_meta *sf_cache_get(zend_string *class_name, HashTable *cache);
void sf_cache_put(zend_string *class_name, sf_class_meta *meta, HashTable *cache);
sf_class_meta *sf_cache_build(zend_string *class_name, zend_class_entry *ce);
void sf_cache_clear(HashTable *cache);

/* Class metadata lifecycle */
sf_class_meta *sf_class_meta_create(zend_string *class_name);
void sf_class_meta_destroy(sf_class_meta *meta);
void sf_class_meta_addref(sf_class_meta *meta);
void sf_class_meta_release(sf_class_meta *meta);

/* Parameter info helpers */
sf_param_info *sf_param_info_create(uint32_t count);
void sf_param_info_destroy(sf_param_info *params, uint32_t count);

#endif /* SF_REFLECTION_CACHE_H */
