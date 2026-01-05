/*
 * Signalforge Container Extension
 * src/binding.c - Binding structures
 *
 * A binding connects an "abstract" (interface name, class name, or alias) to
 * a "concrete" (implementation class, closure factory, or existing object).
 *
 * Binding scopes:
 * - TRANSIENT: Fresh instance every Container::make() call
 * - SINGLETON: First call creates, subsequent calls return cached
 * - INSTANCE: User-provided object, stored as-is
 *
 * We use reference counting so bindings can be safely shared and cleaned up
 * when no longer needed.
 */

#include "../php_signalforge_container.h"
#include "binding.h"

/* ============================================================================
 * Regular Bindings
 * ============================================================================ */

sf_binding *sf_binding_create(zend_string *abstract, zval *concrete, uint8_t scope)
{
    sf_binding *b = emalloc(sizeof(sf_binding));
    
    b->abstract = zend_string_copy(abstract);
    ZVAL_COPY(&b->concrete, concrete);
    b->scope = scope;
    b->resolved = 0;
    ZVAL_UNDEF(&b->instance);
    b->refcount = 1;
    
    return b;
}

void sf_binding_destroy(sf_binding *b)
{
    if (!b) return;
    
    zend_string_release(b->abstract);
    zval_ptr_dtor(&b->concrete);
    
    if (!Z_ISUNDEF(b->instance)) {
        zval_ptr_dtor(&b->instance);
    }
    
    efree(b);
}

void sf_binding_addref(sf_binding *b)
{
    if (b) b->refcount++;
}

void sf_binding_release(sf_binding *b)
{
    if (b && --b->refcount == 0) {
        sf_binding_destroy(b);
    }
}

/* ============================================================================
 * Contextual Bindings
 *
 * These are "when X needs Y, give Z" rules. They override the normal binding
 * when resolving dependencies for a specific class.
 *
 * Example: When UserController needs Logger, give FileLogger
 *          When AdminController needs Logger, give DatabaseLogger
 * ============================================================================ */

sf_contextual_binding *sf_contextual_binding_create(zend_string *concrete, zend_string *abstract, zval *impl)
{
    sf_contextual_binding *b = emalloc(sizeof(sf_contextual_binding));
    
    b->concrete = zend_string_copy(concrete);    /* The class that has the dependency */
    b->abstract = zend_string_copy(abstract);    /* The dependency type */
    ZVAL_COPY(&b->implementation, impl);         /* What to inject instead */
    b->refcount = 1;
    
    return b;
}

void sf_contextual_binding_destroy(sf_contextual_binding *b)
{
    if (!b) return;
    
    zend_string_release(b->concrete);
    zend_string_release(b->abstract);
    zval_ptr_dtor(&b->implementation);
    
    efree(b);
}

void sf_contextual_binding_addref(sf_contextual_binding *b)
{
    if (b) b->refcount++;
}

void sf_contextual_binding_release(sf_contextual_binding *b)
{
    if (b && --b->refcount == 0) {
        sf_contextual_binding_destroy(b);
    }
}
