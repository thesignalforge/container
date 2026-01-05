/*
 * Signalforge Container Extension
 * 
 * This file exposes the Container class to PHP. All methods are static because
 * we use a single global container per request - this matches how DI containers
 * are typically used (one container for the entire application lifecycle).
 *
 * The actual container logic lives in src/container.c. This file just bridges
 * PHP's Zend API to our native implementation.
 */

#include "php_signalforge_container.h"
#include "src/container.h"
#include "src/binding.h"
#include "src/reflection_cache.h"
#include "src/autowire.h"
#include "src/compiler.h"

#include <unistd.h>  /* For access() */

/*
 * Module globals store per-request state. In PHP, each web request is isolated,
 * so we need a fresh container for each request. ZEND_DECLARE_MODULE_GLOBALS
 * creates thread-safe storage that PHP manages for us.
 */
ZEND_DECLARE_MODULE_GLOBALS(signalforge_container)

/* Class entry pointers - Zend needs these to create PHP objects */
zend_class_entry *sf_container_ce = NULL;
zend_class_entry *sf_container_exception_ce = NULL;
zend_class_entry *sf_not_found_exception_ce = NULL;
zend_class_entry *sf_circular_dependency_exception_ce = NULL;
zend_class_entry *sf_contextual_builder_ce = NULL;

/* Custom object handlers let us hook into object lifecycle (create/destroy) */
zend_object_handlers sf_container_object_handlers;
zend_object_handlers sf_contextual_builder_object_handlers;

/* Compiled container reference (PHP object) */
static zval sf_compiled_container;
static zend_bool sf_compiled_container_active = 0;

/* ============================================================================
 * Object Lifecycle
 * 
 * PHP objects in C extensions need custom create/free handlers. The pattern:
 * 1. Allocate our struct that embeds zend_object at the END (required by Zend)
 * 2. Initialize the zend_object part
 * 3. Set our custom handlers
 * 4. Return pointer to the embedded zend_object
 * ============================================================================ */

static zend_object *sf_container_object_create(zend_class_entry *ce)
{
    /* zend_object_alloc handles alignment and adds space for properties */
    sf_container_object *intern = zend_object_alloc(sizeof(sf_container_object), ce);
    
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    
    intern->container = NULL;
    intern->std.handlers = &sf_container_object_handlers;
    
    return &intern->std;
}

static void sf_container_object_free(zend_object *obj)
{
    /* XtOffsetOf calculates the offset from zend_object back to our struct */
    sf_container_object *intern = (sf_container_object *)((char *)obj - XtOffsetOf(sf_container_object, std));
    
    /* Don't free the global container - it's managed by module globals */
    if (intern->container && intern->container != SF_CONTAINER_G(global_container)) {
        sf_container_release(intern->container);
    }
    
    zend_object_std_dtor(&intern->std);
}

/*
 * ContextualBuilder is a fluent builder for contextual bindings:
 *   Container::when(UserController::class)->needs(Logger::class)->give(FileLogger::class)
 * 
 * It stores state between method calls (concrete class and abstract dependency).
 */
static zend_object *sf_contextual_builder_object_create(zend_class_entry *ce)
{
    sf_contextual_builder_object *intern = zend_object_alloc(sizeof(sf_contextual_builder_object), ce);
    
    zend_object_std_init(&intern->std, ce);
    object_properties_init(&intern->std, ce);
    
    /* Initialize to NULL - will be set by when() and needs() */
    intern->concrete = NULL;
    intern->abstract = NULL;
    intern->container = NULL;
    intern->std.handlers = &sf_contextual_builder_object_handlers;
    
    return &intern->std;
}

static void sf_contextual_builder_object_free(zend_object *obj)
{
    sf_contextual_builder_object *intern = (sf_contextual_builder_object *)((char *)obj - XtOffsetOf(sf_contextual_builder_object, std));
    
    /* Release zend_strings - they're reference counted */
    if (intern->concrete) {
        zend_string_release(intern->concrete);
    }
    if (intern->abstract) {
        zend_string_release(intern->abstract);
    }
    if (intern->container) {
        sf_container_release(intern->container);
    }
    
    zend_object_std_dtor(&intern->std);
}

/* ============================================================================
 * Global Container Access
 * 
 * The container is created lazily on first access. This avoids allocating
 * memory if the extension is loaded but not used.
 * ============================================================================ */

static inline sf_container *sf_get_global_container(void)
{
    if (!SF_CONTAINER_G(global_container)) {
        SF_CONTAINER_G(global_container) = sf_container_create();
    }
    return SF_CONTAINER_G(global_container);
}

/* ============================================================================
 * Argument Info (PHP type declarations)
 * 
 * These macros generate the arginfo structs that PHP uses for:
 * - Reflection API
 * - Type checking at runtime
 * - IDE autocompletion via stubs
 * ============================================================================ */

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_bind, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, concrete, IS_MIXED, 0, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_singleton, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, concrete, IS_MIXED, 0, "null")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_instance, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, instance, IS_OBJECT, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_make, 0, 1, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, parameters, IS_ARRAY, 0, "[]")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_get, 0, 1, IS_MIXED, 0)
    ZEND_ARG_TYPE_INFO(0, id, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_has, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, id, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_bound, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_resolved, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_alias, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO(0, alias, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_tag, 0, 2, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstracts, IS_ARRAY, 0)
    ZEND_ARG_TYPE_INFO(0, tag, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_tagged, 0, 1, IS_ARRAY, 0)
    ZEND_ARG_TYPE_INFO(0, tag, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_container_when, 0, 1, Signalforge\\Container\\ContextualBuilder, 0)
    ZEND_ARG_TYPE_INFO(0, concrete, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_flush, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_forget_instance, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_forget_instances, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_compile, 0, 0, IS_LONG, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_is_compiled, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_clear_compiled, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_get_bindings, 0, 0, IS_ARRAY, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_get_metadata, 0, 1, IS_ARRAY, 1)
    ZEND_ARG_TYPE_INFO(0, className, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_dump, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, className, IS_STRING, 0, "\"CompiledContainer\"")
    ZEND_ARG_TYPE_INFO_WITH_DEFAULT_VALUE(0, namespace, IS_STRING, 0, "\"\"")
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_load_compiled, 0, 1, _IS_BOOL, 0)
    ZEND_ARG_TYPE_INFO(0, path, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_unload_compiled, 0, 0, IS_VOID, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_has_compiled, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_clear_cache, 0, 0, _IS_BOOL, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_container_get_cache_path, 0, 0, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_OBJ_INFO_EX(arginfo_contextual_builder_needs, 0, 1, Signalforge\\Container\\ContextualBuilder, 0)
    ZEND_ARG_TYPE_INFO(0, abstract, IS_STRING, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_WITH_RETURN_TYPE_INFO_EX(arginfo_contextual_builder_give, 0, 1, IS_VOID, 0)
    ZEND_ARG_TYPE_INFO(0, implementation, IS_MIXED, 0)
ZEND_END_ARG_INFO()

/* ============================================================================
 * Container Methods
 * ============================================================================ */

/*
 * Shared implementation for bind() and singleton() - they only differ by scope.
 * Using INTERNAL_FUNCTION_PARAM_PASSTHRU avoids duplicating parameter parsing.
 */
static void sf_do_bind(INTERNAL_FUNCTION_PARAMETERS, uint8_t scope)
{
    zend_string *abstract;
    zval *concrete = NULL;
    
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STR(abstract)
        Z_PARAM_OPTIONAL
        Z_PARAM_ZVAL(concrete)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container *container = sf_get_global_container();
    
    /*
     * If no concrete given OR concrete is null, bind abstract to itself.
     * This handles both:
     *   Container::bind(Foo::class)       - concrete pointer is NULL
     *   Container::bind(Foo::class, null) - concrete is IS_NULL zval
     */
    zval concrete_val;
    if (!concrete || Z_TYPE_P(concrete) == IS_NULL) {
        ZVAL_STR(&concrete_val, zend_string_copy(abstract));
        concrete = &concrete_val;
    }
    
    sf_container_bind(container, abstract, concrete, scope);
    
    /* Clean up temporary zval if we created one */
    if (concrete == &concrete_val) {
        zval_ptr_dtor(&concrete_val);
    }
}

/* Container::bind() - creates new instance each resolution (transient) */
PHP_METHOD(Container, bind)
{
    sf_do_bind(INTERNAL_FUNCTION_PARAM_PASSTHRU, SF_SCOPE_TRANSIENT);
}

/* Container::singleton() - caches first instance, returns same one after */
PHP_METHOD(Container, singleton)
{
    sf_do_bind(INTERNAL_FUNCTION_PARAM_PASSTHRU, SF_SCOPE_SINGLETON);
}

/* Container::instance() - store an already-constructed object */
PHP_METHOD(Container, instance)
{
    zend_string *abstract;
    zval *instance;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STR(abstract)
        Z_PARAM_OBJECT(instance)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container_instance(sf_get_global_container(), abstract, instance);
}

/*
 * Container::make() - the core resolution method.
 * Resolves an abstract to a concrete instance, autowiring dependencies.
 */
PHP_METHOD(Container, make)
{
    zend_string *abstract;
    HashTable *parameters = NULL;
    
    ZEND_PARSE_PARAMETERS_START(1, 2)
        Z_PARAM_STR(abstract)
        Z_PARAM_OPTIONAL
        Z_PARAM_ARRAY_HT(parameters)
    ZEND_PARSE_PARAMETERS_END();
    
    if (sf_container_make(sf_get_global_container(), abstract, parameters, return_value, NULL) == FAILURE) {
        RETURN_NULL();
    }
}

/* Container::get() - PSR-11 compatible, just calls make() without parameters */
PHP_METHOD(Container, get)
{
    zend_string *id;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(id)
    ZEND_PARSE_PARAMETERS_END();
    
    if (sf_container_make(sf_get_global_container(), id, NULL, return_value, NULL) == FAILURE) {
        RETURN_NULL();
    }
}

/* Container::has() - PSR-11 compatible, checks if resolvable */
PHP_METHOD(Container, has)
{
    zend_string *id;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(id)
    ZEND_PARSE_PARAMETERS_END();
    
    RETURN_BOOL(sf_container_has(sf_get_global_container(), id));
}

/* Container::bound() - checks if explicitly bound (not just auto-resolvable) */
PHP_METHOD(Container, bound)
{
    zend_string *abstract;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(abstract)
    ZEND_PARSE_PARAMETERS_END();
    
    RETURN_BOOL(sf_container_bound(sf_get_global_container(), abstract));
}

/* Container::resolved() - checks if singleton has been instantiated */
PHP_METHOD(Container, resolved)
{
    zend_string *abstract;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(abstract)
    ZEND_PARSE_PARAMETERS_END();
    
    RETURN_BOOL(sf_container_resolved(sf_get_global_container(), abstract));
}

/* Container::alias() - create alternate name for a binding */
PHP_METHOD(Container, alias)
{
    zend_string *abstract, *alias;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_STR(abstract)
        Z_PARAM_STR(alias)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container_alias(sf_get_global_container(), abstract, alias);
}

/* Container::tag() - group multiple abstracts under a tag name */
PHP_METHOD(Container, tag)
{
    HashTable *abstracts;
    zend_string *tag;
    
    ZEND_PARSE_PARAMETERS_START(2, 2)
        Z_PARAM_ARRAY_HT(abstracts)
        Z_PARAM_STR(tag)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container_tag(sf_get_global_container(), abstracts, tag);
}

/* Container::tagged() - resolve all abstracts with given tag */
PHP_METHOD(Container, tagged)
{
    zend_string *tag;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(tag)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container_tagged(sf_get_global_container(), tag, return_value);
}

/*
 * Container::when() - start building a contextual binding.
 * Returns a ContextualBuilder that captures the "when this class" part.
 */
PHP_METHOD(Container, when)
{
    zend_string *concrete;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(concrete)
    ZEND_PARSE_PARAMETERS_END();
    
    /* Create and return a ContextualBuilder object */
    object_init_ex(return_value, sf_contextual_builder_ce);
    sf_contextual_builder_object *builder = Z_CONTEXTUAL_BUILDER_OBJ_P(return_value);
    
    /* Store the concrete class and a reference to the container */
    builder->concrete = zend_string_copy(concrete);
    builder->container = sf_get_global_container();
    sf_container_addref(builder->container);
}

/* Container::flush() - clear all bindings and cached instances */
PHP_METHOD(Container, flush)
{
    ZEND_PARSE_PARAMETERS_NONE();
    sf_container_flush(sf_get_global_container());
}

/* Container::forgetInstance() - remove cached singleton for rebinding */
PHP_METHOD(Container, forgetInstance)
{
    zend_string *abstract;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(abstract)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container_forget_instance(sf_get_global_container(), abstract);
}

/* Container::forgetInstances() - remove all cached singletons */
PHP_METHOD(Container, forgetInstances)
{
    ZEND_PARSE_PARAMETERS_NONE();
    sf_container_forget_instances(sf_get_global_container());
}

/* Container::compile() - compile all bindings for faster resolution */
PHP_METHOD(Container, compile)
{
    ZEND_PARSE_PARAMETERS_NONE();
    
    int count = sf_compiler_compile_all(sf_get_global_container());
    RETURN_LONG(count);
}

/* Container::isCompiled() - check if compilation mode is enabled */
PHP_METHOD(Container, isCompiled)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(sf_container_is_compiled(sf_get_global_container()));
}

/* Container::clearCompiled() - clear compiled factories and disable compilation */
PHP_METHOD(Container, clearCompiled)
{
    ZEND_PARSE_PARAMETERS_NONE();
    sf_container_clear_compiled(sf_get_global_container());
}

/* Container::getBindings() - export all bindings for code generation */
PHP_METHOD(Container, getBindings)
{
    ZEND_PARSE_PARAMETERS_NONE();
    
    sf_container *c = sf_get_global_container();
    
    array_init(return_value);
    
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(&c->bindings, key, val) {
        sf_binding *binding = (sf_binding *)Z_PTR_P(val);
        
        zval binding_info;
        array_init(&binding_info);
        
        /* abstract */
        add_assoc_str(&binding_info, "abstract", zend_string_copy(binding->abstract));
        
        /* concrete - could be string, closure, or object */
        zval concrete_copy;
        ZVAL_COPY(&concrete_copy, &binding->concrete);
        add_assoc_zval(&binding_info, "concrete", &concrete_copy);
        
        /* scope */
        const char *scope_str;
        switch (binding->scope) {
            case SF_SCOPE_SINGLETON: scope_str = "singleton"; break;
            case SF_SCOPE_INSTANCE: scope_str = "instance"; break;
            default: scope_str = "transient"; break;
        }
        add_assoc_string(&binding_info, "scope", scope_str);
        
        /* resolved */
        add_assoc_bool(&binding_info, "resolved", binding->resolved);
        
        add_assoc_zval(return_value, ZSTR_VAL(key), &binding_info);
    } ZEND_HASH_FOREACH_END();
}

/* Container::getMetadata() - get reflection metadata for a class */
PHP_METHOD(Container, getMetadata)
{
    zend_string *class_name;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(class_name)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_container *c = sf_get_global_container();
    
    /* Look up class entry */
    zend_class_entry *ce = zend_lookup_class(class_name);
    if (!ce) {
        RETURN_NULL();
    }
    
    /* Get or build metadata */
    sf_class_meta *meta = sf_cache_get(class_name, &c->reflection_cache);
    if (!meta) {
        meta = sf_cache_build(class_name, ce);
        if (meta) {
            sf_cache_put(class_name, meta, &c->reflection_cache);
        }
    }
    
    if (!meta) {
        RETURN_NULL();
    }
    
    array_init(return_value);
    
    add_assoc_str(return_value, "class", zend_string_copy(meta->class_name));
    add_assoc_bool(return_value, "instantiable", meta->is_instantiable);
    add_assoc_long(return_value, "paramCount", meta->param_count);
    
    /* Parameters */
    zval params_arr;
    array_init(&params_arr);
    
    for (uint32_t i = 0; i < meta->param_count; i++) {
        sf_param_info *param = &meta->params[i];
        
        zval param_info;
        array_init(&param_info);
        
        add_assoc_str(&param_info, "name", zend_string_copy(param->name));
        
        if (param->type_hint) {
            add_assoc_str(&param_info, "type", zend_string_copy(param->type_hint));
        } else {
            add_assoc_null(&param_info, "type");
        }
        
        add_assoc_bool(&param_info, "nullable", param->is_nullable);
        add_assoc_bool(&param_info, "hasDefault", param->has_default);
        add_assoc_bool(&param_info, "variadic", param->is_variadic);
        
        if (param->has_default) {
            zval default_copy;
            ZVAL_COPY(&default_copy, &param->default_value);
            add_assoc_zval(&param_info, "default", &default_copy);
        }
        
        add_next_index_zval(&params_arr, &param_info);
    }
    
    add_assoc_zval(return_value, "params", &params_arr);
}

/* Container::dump() - generate compiled container PHP file */
PHP_METHOD(Container, dump)
{
    zend_string *path;
    zend_string *class_name = NULL;
    zend_string *namespace = NULL;
    zend_bool eager = 0;
    
    ZEND_PARSE_PARAMETERS_START(1, 4)
        Z_PARAM_STR(path)
        Z_PARAM_OPTIONAL
        Z_PARAM_STR(class_name)
        Z_PARAM_STR(namespace)
        Z_PARAM_BOOL(eager)
    ZEND_PARSE_PARAMETERS_END();
    
    /* Default values */
    if (!class_name) {
        class_name = zend_string_init("CompiledContainer", sizeof("CompiledContainer") - 1, 0);
    } else {
        zend_string_addref(class_name);
    }
    if (!namespace) {
        namespace = zend_string_init("", 0, 0);
    } else {
        zend_string_addref(namespace);
    }
    
    /* Look up ContainerDumper class */
    zend_string *dumper_class_name = zend_string_init("Signalforge\\Container\\ContainerDumper", sizeof("Signalforge\\Container\\ContainerDumper") - 1, 0);
    zend_class_entry *dumper_ce = zend_lookup_class(dumper_class_name);
    zend_string_release(dumper_class_name);
    
    if (!dumper_ce) {
        zend_string_release(class_name);
        zend_string_release(namespace);
        zend_throw_exception(sf_container_exception_ce, "ContainerDumper class not found. Make sure to include the container-php package.", 0);
        RETURN_FALSE;
    }
    
    /* Create dumper instance */
    zval dumper;
    object_init_ex(&dumper, dumper_ce);
    
    /* Call dumpToFile method using zend_call_function */
    zval retval;
    zval args[4];
    ZVAL_STR(&args[0], path);
    ZVAL_STR(&args[1], class_name);
    ZVAL_STR(&args[2], namespace);
    ZVAL_BOOL(&args[3], eager);
    
    zend_fcall_info fci = {0};
    zend_fcall_info_cache fcc = {0};
    
    fci.size = sizeof(fci);
    ZVAL_STRING(&fci.function_name, "dumpToFile");
    fci.object = Z_OBJ(dumper);
    fci.retval = &retval;
    fci.params = args;
    fci.param_count = 4;
    
    fcc.function_handler = zend_hash_str_find_ptr(&dumper_ce->function_table, "dumptofile", sizeof("dumptofile") - 1);
    fcc.called_scope = dumper_ce;
    fcc.object = Z_OBJ(dumper);
    
    zend_call_function(&fci, &fcc);
    
    zval_ptr_dtor(&fci.function_name);
    zval_ptr_dtor(&dumper);
    zend_string_release(class_name);
    zend_string_release(namespace);
    
    if (EG(exception)) {
        RETURN_FALSE;
    }
    
    RETURN_BOOL(Z_TYPE(retval) == IS_TRUE || (Z_TYPE(retval) == IS_LONG && Z_LVAL(retval)));
}

/* Container::loadCompiled() - load and activate a compiled container */
PHP_METHOD(Container, loadCompiled)
{
    zend_string *path;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(path)
    ZEND_PARSE_PARAMETERS_END();
    
    /* Check if file exists */
    if (access(ZSTR_VAL(path), F_OK) != 0) {
        zend_throw_exception_ex(sf_container_exception_ce, 0, "Compiled container file not found: %s", ZSTR_VAL(path));
        RETURN_FALSE;
    }
    
    /* Include the file to load the class */
    zend_file_handle file_handle;
    zend_stream_init_filename(&file_handle, ZSTR_VAL(path));
    
    zend_op_array *op_array = zend_compile_file(&file_handle, ZEND_INCLUDE);
    zend_destroy_file_handle(&file_handle);
    
    if (!op_array) {
        zend_throw_exception_ex(sf_container_exception_ce, 0, "Failed to compile container file: %s", ZSTR_VAL(path));
        RETURN_FALSE;
    }
    
    /* Execute the included file */
    zval result;
    ZVAL_UNDEF(&result);
    zend_execute(op_array, &result);
    zval_ptr_dtor(&result);
    destroy_op_array(op_array);
    efree(op_array);
    
    if (EG(exception)) {
        RETURN_FALSE;
    }
    
    /* Look for CompiledContainer class (or namespaced version) */
    zend_class_entry *compiled_ce = NULL;
    
    /* Try common names */
    const char *class_names[] = {
        "CompiledContainer",
        "Signalforge\\Container\\CompiledContainer",
        NULL
    };
    
    for (int i = 0; class_names[i] != NULL; i++) {
        zend_string *cn = zend_string_init(class_names[i], strlen(class_names[i]), 0);
        compiled_ce = zend_lookup_class(cn);
        zend_string_release(cn);
        
        if (compiled_ce) {
            /* Make sure it's not the base class */
            zend_string *base_name = zend_string_init("Signalforge\\Container\\CompiledContainer", sizeof("Signalforge\\Container\\CompiledContainer") - 1, 0);
            zend_class_entry *base_ce = zend_lookup_class(base_name);
            zend_string_release(base_name);
            
            if (compiled_ce != base_ce) {
                break;
            }
            compiled_ce = NULL;
        }
    }
    
    if (!compiled_ce) {
        zend_throw_exception(sf_container_exception_ce, "No CompiledContainer class found in the loaded file", 0);
        RETURN_FALSE;
    }
    
    /* Clean up previous compiled container if any */
    if (sf_compiled_container_active) {
        zval_ptr_dtor(&sf_compiled_container);
        sf_compiled_container_active = 0;
    }
    
    /* Create instance of compiled container */
    object_init_ex(&sf_compiled_container, compiled_ce);
    
    /* Call activate() method */
    zval activate_ret;
    zend_call_method_with_0_params(Z_OBJ(sf_compiled_container), compiled_ce, NULL, "activate", &activate_ret);
    zval_ptr_dtor(&activate_ret);
    
    sf_compiled_container_active = 1;
    
    RETURN_TRUE;
}

/* Container::unloadCompiled() - deactivate and unload compiled container */
PHP_METHOD(Container, unloadCompiled)
{
    ZEND_PARSE_PARAMETERS_NONE();
    
    if (sf_compiled_container_active) {
        /* Call deactivate() method */
        zend_class_entry *ce = Z_OBJCE(sf_compiled_container);
        zval deactivate_ret;
        zend_call_method_with_0_params(Z_OBJ(sf_compiled_container), ce, NULL, "deactivate", &deactivate_ret);
        zval_ptr_dtor(&deactivate_ret);
        
        zval_ptr_dtor(&sf_compiled_container);
        ZVAL_UNDEF(&sf_compiled_container);
        sf_compiled_container_active = 0;
    }
}

/* Container::hasCompiled() - check if a compiled container is loaded */
PHP_METHOD(Container, hasCompiled)
{
    ZEND_PARSE_PARAMETERS_NONE();
    RETURN_BOOL(sf_compiled_container_active);
}

PHP_METHOD(Container, clearCache)
{
    ZEND_PARSE_PARAMETERS_NONE();
    
    if (sf_container_clear_cache(SF_CONTAINER_G(global_container)) == SUCCESS) {
        RETURN_TRUE;
    }
    RETURN_FALSE;
}

PHP_METHOD(Container, getCachePath)
{
    ZEND_PARSE_PARAMETERS_NONE();
    
    zend_string *path = sf_container_get_cache_path(SF_CONTAINER_G(global_container));
    RETURN_STR(path);
}

/* ============================================================================
 * ContextualBuilder Methods
 * 
 * Fluent interface: when(A)->needs(B)->give(C)
 * Means: when resolving class A, if it needs B, give it C instead
 * ============================================================================ */

/* ContextualBuilder::needs() - specify what dependency to override */
PHP_METHOD(ContextualBuilder, needs)
{
    zend_string *abstract;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_STR(abstract)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_contextual_builder_object *builder = Z_CONTEXTUAL_BUILDER_OBJ_P(ZEND_THIS);
    
    /* Release previous if needs() called multiple times */
    if (builder->abstract) {
        zend_string_release(builder->abstract);
    }
    builder->abstract = zend_string_copy(abstract);
    
    /* Return $this for chaining */
    RETURN_ZVAL(ZEND_THIS, 1, 0);
}

/* ContextualBuilder::give() - provide the implementation to use */
PHP_METHOD(ContextualBuilder, give)
{
    zval *implementation;
    
    ZEND_PARSE_PARAMETERS_START(1, 1)
        Z_PARAM_ZVAL(implementation)
    ZEND_PARSE_PARAMETERS_END();
    
    sf_contextual_builder_object *builder = Z_CONTEXTUAL_BUILDER_OBJ_P(ZEND_THIS);
    
    /* Validate that needs() was called first */
    if (!builder->abstract) {
        zend_throw_exception(sf_container_exception_ce, "needs() must be called before give()", 0);
        RETURN_NULL();
    }
    
    /* Register the contextual binding */
    sf_container_add_contextual_binding(builder->container, builder->concrete, builder->abstract, implementation);
}

/* ============================================================================
 * Method Registration Tables
 * 
 * Maps PHP method names to C functions with their arginfo.
 * ZEND_ACC_STATIC makes them callable as Container::method()
 * ============================================================================ */

static const zend_function_entry sf_container_methods[] = {
    PHP_ME(Container, bind, arginfo_container_bind, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, singleton, arginfo_container_singleton, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, instance, arginfo_container_instance, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, make, arginfo_container_make, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, get, arginfo_container_get, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, has, arginfo_container_has, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, bound, arginfo_container_bound, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, resolved, arginfo_container_resolved, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, alias, arginfo_container_alias, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, tag, arginfo_container_tag, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, tagged, arginfo_container_tagged, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, when, arginfo_container_when, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, flush, arginfo_container_flush, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, forgetInstance, arginfo_container_forget_instance, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, forgetInstances, arginfo_container_forget_instances, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, compile, arginfo_container_compile, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, isCompiled, arginfo_container_is_compiled, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, clearCompiled, arginfo_container_clear_compiled, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, getBindings, arginfo_container_get_bindings, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, getMetadata, arginfo_container_get_metadata, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, dump, arginfo_container_dump, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, loadCompiled, arginfo_container_load_compiled, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, unloadCompiled, arginfo_container_unload_compiled, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, hasCompiled, arginfo_container_has_compiled, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, clearCache, arginfo_container_clear_cache, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_ME(Container, getCachePath, arginfo_container_get_cache_path, ZEND_ACC_PUBLIC | ZEND_ACC_STATIC)
    PHP_FE_END
};

static const zend_function_entry sf_contextual_builder_methods[] = {
    PHP_ME(ContextualBuilder, needs, arginfo_contextual_builder_needs, ZEND_ACC_PUBLIC)
    PHP_ME(ContextualBuilder, give, arginfo_contextual_builder_give, ZEND_ACC_PUBLIC)
    PHP_FE_END
};

/* ============================================================================
 * Class Registration
 * 
 * Called once at module init to register our classes with PHP.
 * Exception hierarchy: ContainerException -> NotFoundException
 *                                         -> CircularDependencyException
 * ============================================================================ */

void sf_register_exception_classes(void)
{
    zend_class_entry ce;
    
    INIT_CLASS_ENTRY(ce, "Signalforge\\Container\\ContainerException", NULL);
    sf_container_exception_ce = zend_register_internal_class_ex(&ce, zend_ce_exception);
    
    INIT_CLASS_ENTRY(ce, "Signalforge\\Container\\NotFoundException", NULL);
    sf_not_found_exception_ce = zend_register_internal_class_ex(&ce, sf_container_exception_ce);
    
    INIT_CLASS_ENTRY(ce, "Signalforge\\Container\\CircularDependencyException", NULL);
    sf_circular_dependency_exception_ce = zend_register_internal_class_ex(&ce, sf_container_exception_ce);
}

void sf_register_container_class(void)
{
    zend_class_entry ce;
    
    INIT_CLASS_ENTRY(ce, "Signalforge\\Container\\Container", sf_container_methods);
    sf_container_ce = zend_register_internal_class(&ce);
    sf_container_ce->ce_flags |= ZEND_ACC_FINAL;  /* Prevent subclassing */
    sf_container_ce->create_object = sf_container_object_create;
    
    /* Copy default handlers then customize */
    memcpy(&sf_container_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sf_container_object_handlers.offset = XtOffsetOf(sf_container_object, std);
    sf_container_object_handlers.free_obj = sf_container_object_free;
}

void sf_register_contextual_builder_class(void)
{
    zend_class_entry ce;
    
    INIT_CLASS_ENTRY(ce, "Signalforge\\Container\\ContextualBuilder", sf_contextual_builder_methods);
    sf_contextual_builder_ce = zend_register_internal_class(&ce);
    sf_contextual_builder_ce->ce_flags |= ZEND_ACC_FINAL;
    sf_contextual_builder_ce->create_object = sf_contextual_builder_object_create;
    
    memcpy(&sf_contextual_builder_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
    sf_contextual_builder_object_handlers.offset = XtOffsetOf(sf_contextual_builder_object, std);
    sf_contextual_builder_object_handlers.free_obj = sf_contextual_builder_object_free;
}

/* ============================================================================
 * Module Lifecycle Hooks
 * 
 * PHP extension lifecycle:
 * - GINIT:    Once per thread/process, init globals to safe defaults
 * - MINIT:    Module load, register classes (once per PHP startup)
 * - RINIT:    Request start, reset per-request state
 * - RSHUTDOWN: Request end, cleanup per-request resources
 * - MSHUTDOWN: Module unload (PHP shutdown)
 * - GSHUTDOWN: Thread/process exit, cleanup globals
 * ============================================================================ */

static PHP_GINIT_FUNCTION(signalforge_container)
{
#if defined(COMPILE_DL_SIGNALFORGE_CONTAINER) && defined(ZTS)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    signalforge_container_globals->global_container = NULL;
}

static PHP_GSHUTDOWN_FUNCTION(signalforge_container)
{
    if (signalforge_container_globals->global_container) {
        sf_container_release(signalforge_container_globals->global_container);
        signalforge_container_globals->global_container = NULL;
    }
}

PHP_MINIT_FUNCTION(signalforge_container)
{
#if defined(ZTS) && defined(COMPILE_DL_SIGNALFORGE_CONTAINER)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif

    /* Exception classes must be registered first (base before derived) */
    sf_register_exception_classes();
    sf_register_container_class();
    sf_register_contextual_builder_class();
    
    return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(signalforge_container)
{
    return SUCCESS;
}

/* Called at start of each request - reset container state */
PHP_RINIT_FUNCTION(signalforge_container)
{
#if defined(ZTS) && defined(COMPILE_DL_SIGNALFORGE_CONTAINER)
    ZEND_TSRMLS_CACHE_UPDATE();
#endif
    
    SF_CONTAINER_G(global_container) = NULL;  /* Will be created lazily */
    
    return SUCCESS;
}

/* Called at end of each request - cleanup */
PHP_RSHUTDOWN_FUNCTION(signalforge_container)
{
    if (SF_CONTAINER_G(global_container)) {
        sf_container_release(SF_CONTAINER_G(global_container));
        SF_CONTAINER_G(global_container) = NULL;
    }
    
    return SUCCESS;
}

PHP_MINFO_FUNCTION(signalforge_container)
{
    php_info_print_table_start();
    php_info_print_table_header(2, "signalforge_container support", "enabled");
    php_info_print_table_row(2, "Version", PHP_SIGNALFORGE_CONTAINER_VERSION);
    php_info_print_table_end();
}

/* Module entry - tells PHP everything about this extension */
zend_module_entry signalforge_container_module_entry = {
    STANDARD_MODULE_HEADER,
    PHP_SIGNALFORGE_CONTAINER_EXTNAME,
    NULL,                                      /* No global functions */
    PHP_MINIT(signalforge_container),
    PHP_MSHUTDOWN(signalforge_container),
    PHP_RINIT(signalforge_container),
    PHP_RSHUTDOWN(signalforge_container),
    PHP_MINFO(signalforge_container),
    PHP_SIGNALFORGE_CONTAINER_VERSION,
    PHP_MODULE_GLOBALS(signalforge_container),
    PHP_GINIT(signalforge_container),
    PHP_GSHUTDOWN(signalforge_container),
    NULL,
    STANDARD_MODULE_PROPERTIES_EX
};

/* Dynamic loading support - makes the extension loadable via extension= */
#ifdef COMPILE_DL_SIGNALFORGE_CONTAINER
#ifdef ZTS
ZEND_TSRMLS_CACHE_DEFINE()
#endif
ZEND_GET_MODULE(signalforge_container)
#endif
