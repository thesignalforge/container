/*
 * Signalforge Container Extension
 * src/binding.h - Binding structures
 */

#ifndef SF_BINDING_H
#define SF_BINDING_H

/* Maps an abstract (interface/class name) to a concrete implementation
 * Layout optimized for cache line alignment (hot fields first) */
struct _sf_binding {
    /* Hot fields (accessed during resolution) - first cache line */
    zend_string *abstract;  /* What you ask for */
    zval concrete;          /* What you get (class name, closure, or object) */
    zval instance;          /* Cached instance for singleton scope */
    
    /* Cold fields (accessed less frequently) */
    uint32_t refcount;
    uint8_t scope;          /* SF_SCOPE_TRANSIENT, _SINGLETON, or _INSTANCE */
    zend_bool resolved;     /* Has been resolved at least once (for singletons) */
    uint8_t _padding[2];    /* Align to 8 bytes */
} __attribute__((aligned(64)));

/* Context-specific binding: when A needs B, give C instead of default B
 * Layout optimized for cache line alignment */
struct _sf_contextual_binding {
    /* Hot fields */
    zend_string *concrete;  /* The class that has the dependency (A) */
    zend_string *abstract;  /* The dependency it needs (B) */
    zval implementation;    /* What to give it (C) */
    
    /* Cold fields */
    uint32_t refcount;
    uint8_t _padding[4];    /* Align to 8 bytes */
} __attribute__((aligned(64)));

/* Regular binding lifecycle */
sf_binding *sf_binding_create(zend_string *abstract, zval *concrete, uint8_t scope);
void sf_binding_destroy(sf_binding *binding);
void sf_binding_addref(sf_binding *binding);
void sf_binding_release(sf_binding *binding);

/* Contextual binding lifecycle */
sf_contextual_binding *sf_contextual_binding_create(zend_string *concrete, zend_string *abstract, zval *implementation);
void sf_contextual_binding_destroy(sf_contextual_binding *binding);
void sf_contextual_binding_addref(sf_contextual_binding *binding);
void sf_contextual_binding_release(sf_contextual_binding *binding);

#endif /* SF_BINDING_H */
