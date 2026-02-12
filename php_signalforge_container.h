/*
 * Signalforge Container Extension
 * php_signalforge_container.h - Main extension header
 *
 * Defines all public types and the module entry. Include this from any .c file
 * that needs container types or Zend API functions.
 */

#ifndef PHP_SIGNALFORGE_CONTAINER_H
#define PHP_SIGNALFORGE_CONTAINER_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Core PHP/Zend headers */
#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "zend_exceptions.h"
#include "zend_interfaces.h"
#include "zend_smart_str.h"
#include "zend_closures.h"

/* Module entry - used by PHP to load the extension */
extern zend_module_entry signalforge_container_module_entry;
#define phpext_signalforge_container_ptr &signalforge_container_module_entry

#define PHP_SIGNALFORGE_CONTAINER_VERSION "1.0.0"
#define PHP_SIGNALFORGE_CONTAINER_EXTNAME "signalforge_container"

/* Export/visibility macros for shared library symbols */
#ifdef PHP_WIN32
#   define PHP_SIGNALFORGE_CONTAINER_API __declspec(dllexport)
#elif defined(__GNUC__) && __GNUC__ >= 4
#   define PHP_SIGNALFORGE_CONTAINER_API __attribute__ ((visibility("default")))
#else
#   define PHP_SIGNALFORGE_CONTAINER_API
#endif

/* Compiler optimization hints - fallback if not defined by Zend */
#ifndef ZEND_HOT
#   if defined(__GNUC__) && __GNUC__ >= 4
#       define ZEND_HOT __attribute__((hot))
#   else
#       define ZEND_HOT
#   endif
#endif

#ifdef ZTS
#include "TSRM.h"
#endif

/* Forward declarations - full definitions in respective headers */
typedef struct _sf_container sf_container;
typedef struct _sf_binding sf_binding;
typedef struct _sf_class_meta sf_class_meta;
typedef struct _sf_param_info sf_param_info;
typedef struct _sf_resolution_context sf_resolution_context;
typedef struct _sf_contextual_binding sf_contextual_binding;
typedef struct _sf_factory sf_factory;

/* ============================================================================
 * Module Globals
 *
 * Per-request storage for the global container. ZTS builds use thread-local
 * storage, non-ZTS uses a simple global.
 * ============================================================================ */

ZEND_BEGIN_MODULE_GLOBALS(signalforge_container)
    sf_container *global_container;  /* Lazily created on first use */
ZEND_END_MODULE_GLOBALS(signalforge_container)

/* Accessor macro - use this instead of accessing globals directly */
#ifdef ZTS
#define SF_CONTAINER_G(v) TSRMG(signalforge_container_globals_id, zend_signalforge_container_globals *, v)
#else
#define SF_CONTAINER_G(v) (signalforge_container_globals.v)
#endif

ZEND_EXTERN_MODULE_GLOBALS(signalforge_container)

/* Class entries - needed for object creation and instanceof checks */
extern zend_class_entry *sf_container_ce;
extern zend_class_entry *sf_container_exception_ce;
extern zend_class_entry *sf_not_found_exception_ce;
extern zend_class_entry *sf_circular_dependency_exception_ce;
extern zend_class_entry *sf_contextual_builder_ce;

/* Custom object handlers */
extern zend_object_handlers sf_container_object_handlers;
extern zend_object_handlers sf_contextual_builder_object_handlers;

/* ============================================================================
 * Binding Scope Constants
 *
 * These control object lifecycle:
 * - TRANSIENT: new instance every make() call
 * - SINGLETON: cached after first resolution
 * - INSTANCE: user-provided object, returned as-is
 * ============================================================================ */

#define SF_SCOPE_TRANSIENT 0
#define SF_SCOPE_SINGLETON 1
#define SF_SCOPE_INSTANCE  2

/* ============================================================================
 * PHP Object Wrappers
 *
 * These structs embed zend_object at the END (required by Zend).
 * The pattern allows us to store C data alongside PHP objects.
 * ============================================================================ */

typedef struct {
    sf_container *container;
    zend_object std;  /* Must be last! */
} sf_container_object;

typedef struct {
    zend_string *concrete;   /* From when() - the requesting class */
    zend_string *abstract;   /* From needs() - the dependency type */
    sf_container *container; /* Reference to parent container */
    zend_object std;         /* Must be last! */
} sf_contextual_builder_object;

/*
 * Macros to extract our struct from a zval or zend_object.
 * XtOffsetOf calculates the byte offset from std back to our struct.
 */
#define Z_CONTAINER_OBJ_P(zv) \
    ((sf_container_object *)((char *)(Z_OBJ_P(zv)) - XtOffsetOf(sf_container_object, std)))

#define Z_CONTEXTUAL_BUILDER_OBJ_P(zv) \
    ((sf_contextual_builder_object *)((char *)(Z_OBJ_P(zv)) - XtOffsetOf(sf_contextual_builder_object, std)))

/* Module lifecycle functions */
PHP_MINIT_FUNCTION(signalforge_container);
PHP_MSHUTDOWN_FUNCTION(signalforge_container);
PHP_RINIT_FUNCTION(signalforge_container);
PHP_RSHUTDOWN_FUNCTION(signalforge_container);
PHP_MINFO_FUNCTION(signalforge_container);

/* Class registration (called from MINIT) */
void sf_register_container_class(void);
void sf_register_exception_classes(void);
void sf_register_contextual_builder_class(void);

#endif /* PHP_SIGNALFORGE_CONTAINER_H */
