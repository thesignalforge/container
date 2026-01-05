/*
 * Signalforge Container Extension
 * src/factory.h - Compiled factory structures
 *
 * A factory is a pre-compiled resolution function that bypasses the full
 * autowiring logic. Instead of dynamic reflection and recursive resolution,
 * it directly instantiates the class with known dependencies.
 *
 * This provides ~3x speedup for autowiring operations in production.
 */

#ifndef SF_FACTORY_H
#define SF_FACTORY_H

/* Forward declarations */
struct _sf_container;
struct _sf_class_meta;

/*
 * Compiled factory function signature.
 * Takes container, optional params, and result zval.
 * Returns SUCCESS or FAILURE.
 */
typedef int (*sf_factory_fn)(struct _sf_container *c, HashTable *params, zval *result, void *factory_data);

/*
 * Factory metadata - stores everything needed for fast resolution.
 */
typedef struct _sf_factory {
    zend_string *class_name;          /* FQCN of the class to instantiate */
    zend_class_entry *ce;             /* Cached class entry */
    sf_factory_fn factory_fn;         /* The compiled factory function */
    
    /* Dependency information */
    zend_string **dep_names;          /* Array of dependency class names */
    zend_ulong *dep_hashes;           /* Pre-computed hashes for fast lookup */
    uint32_t dep_count;               /* Number of dependencies */
    
    /* Flags */
    uint8_t is_singleton;             /* Should result be cached? */
    uint8_t has_constructor;          /* Does class have constructor? */
    
    uint32_t refcount;
} sf_factory;

/* Factory lifecycle */
sf_factory *sf_factory_create(zend_string *class_name, zend_class_entry *ce);
void sf_factory_destroy(sf_factory *factory);
void sf_factory_addref(sf_factory *factory);
void sf_factory_release(sf_factory *factory);

/* Factory execution */
int sf_factory_call(sf_factory *factory, struct _sf_container *c, HashTable *params, zval *result);

/* Factory configuration */
void sf_factory_set_dependencies(sf_factory *factory, zend_string **deps, uint32_t count);
void sf_factory_set_singleton(sf_factory *factory, uint8_t is_singleton);

#endif /* SF_FACTORY_H */
