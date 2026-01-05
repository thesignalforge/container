/*
 * Signalforge Container Extension
 * src/autowire.h - Constructor dependency resolution
 */

#ifndef SF_AUTOWIRE_H
#define SF_AUTOWIRE_H

#include "reflection_cache.h"

typedef struct _sf_container sf_container;

/* Resolve a class by analyzing its constructor and injecting dependencies */
int sf_autowire_resolve(zend_string *class_name, zval *result, HashTable *parameters, sf_container *container);

/* Build constructor arguments from cached metadata (internal) */
int sf_autowire_build_args(sf_class_meta *meta, zval *args, HashTable *parameters, sf_container *container, zend_string *requesting_class);

#endif /* SF_AUTOWIRE_H */
