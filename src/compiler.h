/*
 * Signalforge Container Extension
 * src/compiler.h - Factory compilation interface
 *
 * The compiler generates optimized factory functions for classes,
 * bypassing the full autowiring logic at runtime.
 */

#ifndef SF_COMPILER_H
#define SF_COMPILER_H

#include "factory.h"
#include "reflection_cache.h"

/* Forward declarations */
struct _sf_container;

/*
 * Compile a factory for a class based on its metadata.
 * Returns NULL if the class cannot be compiled (e.g., too many deps).
 */
sf_factory *sf_compiler_compile_class(sf_class_meta *meta, zend_class_entry *ce, uint8_t is_singleton);

/*
 * Compile all registered bindings in the container.
 * Returns the number of factories compiled.
 */
int sf_compiler_compile_all(struct _sf_container *container);

/*
 * Check if a class can be compiled.
 * Returns 1 if compilable, 0 otherwise.
 */
int sf_compiler_can_compile(sf_class_meta *meta);

#endif /* SF_COMPILER_H */
