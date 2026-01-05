/*
 * Signalforge Container Extension
 * src/autowire.c - Automatic dependency injection
 *
 * Autowiring analyzes a class's constructor to figure out what dependencies
 * it needs, then resolves them from the container. This is the "magic" that
 * lets you write:
 *
 *   class UserService {
 *       public function __construct(Logger $logger, Database $db) {}
 *   }
 *   $service = Container::make(UserService::class); // Just works!
 *
 * The process:
 * 1. Look up or build class metadata (parameter names, types, defaults)
 * 2. For each parameter, try to resolve from container or use default
 * 3. Call the constructor with resolved arguments
 */

#include "../php_signalforge_container.h"
#include "autowire.h"
#include "container.h"
#include "reflection_cache.h"
#include "compiler.h"
#include "factory.h"
#include "pool.h"

extern zend_class_entry *sf_not_found_exception_ce;

/*
 * Build the argument list for a constructor directly into a zval buffer.
 *
 * For each parameter, we try (in order):
 * 1. Explicit parameter passed by user (Container::make(X, ['param' => value]))
 * 2. Type-hinted class? Resolve it from the container
 * 3. Has default value? Stop here - PHP will use its own defaults for remaining params
 * 4. Is nullable? Use null
 * 5. None of the above? Throw exception
 *
 * Key insight: For optional parameters we can't resolve, we simply stop building
 * the args. PHP will fill in default values for trailing optional parameters
 * automatically when calling the constructor.
 *
 * Returns: number of arguments built, or -1 on failure
 */
static ZEND_HOT int sf_autowire_build_args_direct(sf_class_meta *meta, zval *arg_buffer, HashTable *params, sf_container *c, zend_string *requester)
{
    if (UNEXPECTED(!meta) || EXPECTED(meta->param_count == 0)) {
        return 0;
    }
    
    uint32_t actual_count = 0;
    
    for (uint32_t i = 0; i < meta->param_count; i++) {
        sf_param_info *p = &meta->params[i];
        zval *arg = &arg_buffer[actual_count];
        
        /* 1. Check user-provided parameters first (uncommon) */
        if (UNEXPECTED(params)) {
            zval *provided = zend_hash_find(params, p->name);
            if (UNEXPECTED(provided)) {
                ZVAL_COPY(arg, provided);
                actual_count++;
                continue;
            }
        }
        
        /* 2. Type hint? Try to resolve from container (common path) */
        if (EXPECTED(p->type_hint)) {
            if (EXPECTED(sf_container_make(c, p->type_hint, NULL, arg, requester) == SUCCESS)) {
                actual_count++;
                continue;
            }
            
            /*
             * Resolution failed - if an exception is already pending (e.g.,
             * CircularDependencyException), don't override it with NotFoundException.
             * Let the original exception propagate.
             */
            if (UNEXPECTED(EG(exception))) {
                /* Cleanup already-built args */
                for (uint32_t j = 0; j < actual_count; j++) {
                    zval_ptr_dtor(&arg_buffer[j]);
                }
                return -1;
            }
            
            /* Resolution failed - check fallbacks (uncommon) */
            if (UNEXPECTED(p->is_nullable)) {
                ZVAL_NULL(arg);
                actual_count++;
                continue;
            }
            
            /*
             * Has default? Stop building args here - PHP will use defaults
             * for this and all remaining optional parameters.
             */
            if (EXPECTED(p->has_default)) {
                break;
            }
            
            /* No fallback - fail with helpful message */
            for (uint32_t j = 0; j < actual_count; j++) {
                zval_ptr_dtor(&arg_buffer[j]);
            }
            zend_throw_exception_ex(sf_not_found_exception_ce, 0,
                "Unable to resolve dependency '%s' for parameter '%s' of class '%s'",
                ZSTR_VAL(p->type_hint), ZSTR_VAL(p->name), ZSTR_VAL(meta->class_name));
            return -1;
        }
        
        /*
         * 3. No type hint - if it has a default, stop building args.
         * PHP will use default values for remaining optional parameters.
         */
        if (p->has_default) {
            break;
        }
        
        /* Variadic with nothing to fill - stop here */
        if (p->is_variadic) {
            break;
        }
        
        /* Can't resolve - no type hint, no default */
        for (uint32_t j = 0; j < actual_count; j++) {
            zval_ptr_dtor(&arg_buffer[j]);
        }
        zend_throw_exception_ex(sf_not_found_exception_ce, 0,
            "Unable to resolve parameter '%s' of class '%s' (no type hint or default value)",
            ZSTR_VAL(p->name), ZSTR_VAL(meta->class_name));
        return -1;
    }
    
    return actual_count;
}

/*
 * Resolve a class by autowiring its constructor.
 *
 * Uses cached reflection metadata to avoid calling ReflectionClass repeatedly.
 * The first resolution of a class is slower (builds metadata), subsequent
 * resolutions are fast (cached lookup + instantiation).
 */
ZEND_HOT int sf_autowire_resolve(zend_string *class_name, zval *result, HashTable *params, sf_container *c)
{
    /* Look up the class entry */
    zend_class_entry *ce = zend_lookup_class(class_name);
    if (UNEXPECTED(!ce)) {
        zend_throw_exception_ex(sf_not_found_exception_ce, 0,
            "Class '%s' not found", ZSTR_VAL(class_name));
        return FAILURE;
    }
    
    /* Can't instantiate interfaces, abstract classes, or traits (uncommon) */
    if (UNEXPECTED(ce->ce_flags & (ZEND_ACC_INTERFACE | ZEND_ACC_ABSTRACT | ZEND_ACC_TRAIT))) {
        zend_throw_exception_ex(sf_not_found_exception_ce, 0,
            "Class '%s' is not instantiable", ZSTR_VAL(class_name));
        return FAILURE;
    }
    
    /* Get or build cached metadata - pass ce to avoid duplicate lookup */
    sf_class_meta *meta = sf_cache_get(class_name, &c->reflection_cache);
    if (UNEXPECTED(!meta)) {
        meta = sf_cache_build(class_name, ce);
        if (UNEXPECTED(!meta)) {
            zend_throw_exception_ex(sf_not_found_exception_ce, 0,
                "Unable to build metadata for class '%s'", ZSTR_VAL(class_name));
            return FAILURE;
        }
        sf_cache_put(class_name, meta, &c->reflection_cache);
    }
    
    if (UNEXPECTED(!meta->is_instantiable)) {
        zend_throw_exception_ex(sf_not_found_exception_ce, 0,
            "Class '%s' is not instantiable", ZSTR_VAL(class_name));
        return FAILURE;
    }
    
    /* Build constructor arguments - try pool first, then stack, then heap */
    zval args_stack[8];
    zval *args_buffer = NULL;
    zend_bool from_pool = 0;
    
    if (meta->param_count > 8) {
        /* Try to get from pool for larger sizes */
        sf_pool_manager *pool = sf_pool_get_manager();
        args_buffer = sf_pool_acquire(pool, meta->param_count);
        if (args_buffer) {
            from_pool = 1;
        } else {
            /* Pool exhausted, fall back to heap */
            args_buffer = emalloc(sizeof(zval) * meta->param_count);
        }
    } else {
        /* Small size: use stack buffer */
        args_buffer = args_stack;
    }
    
    int arg_count = sf_autowire_build_args_direct(meta, args_buffer, params, c, class_name);
    if (arg_count < 0) {
        if (from_pool) {
            sf_pool_release(sf_pool_get_manager(), args_buffer, meta->param_count);
        } else if (args_buffer != args_stack) {
            efree(args_buffer);
        }
        return FAILURE;
    }
    
    /* Create the object (should almost always succeed) */
    if (UNEXPECTED(object_init_ex(result, ce) != SUCCESS)) {
        for (int i = 0; i < arg_count; i++) {
            zval_ptr_dtor(&args_buffer[i]);
        }
        if (from_pool) {
            sf_pool_release(sf_pool_get_manager(), args_buffer, meta->param_count);
        } else if (args_buffer != args_stack) {
            efree(args_buffer);
        }
        zend_throw_exception_ex(sf_not_found_exception_ce, 0,
            "Unable to instantiate class '%s'", ZSTR_VAL(class_name));
        return FAILURE;
    }
    
    /* Call constructor if one exists (common for DI classes) */
    if (EXPECTED(ce->constructor)) {
        zval retval;
        zend_fcall_info fci = {0};
        zend_fcall_info_cache fcc = {0};
        
        fci.size = sizeof(fci);
        fci.retval = &retval;
        fci.object = Z_OBJ_P(result);
        ZVAL_UNDEF(&fci.function_name);  /* Not needed when fcc.function_handler is set */
        fci.params = args_buffer;
        fci.param_count = arg_count;
        
        fcc.function_handler = ce->constructor;
        fcc.called_scope = ce;
        fcc.object = Z_OBJ_P(result);
        
        int call_ret = zend_call_function(&fci, &fcc);
        
        /* Cleanup params */
        for (int i = 0; i < arg_count; i++) {
            zval_ptr_dtor(&args_buffer[i]);
        }
        if (from_pool) {
            sf_pool_release(sf_pool_get_manager(), args_buffer, meta->param_count);
        } else if (args_buffer != args_stack) {
            efree(args_buffer);
        }
        zval_ptr_dtor(&retval);
        
        if (UNEXPECTED(call_ret == FAILURE)) {
            zval_ptr_dtor(result);
            return FAILURE;
        }
    } else {
        /* No constructor - cleanup args */
        for (int i = 0; i < arg_count; i++) {
            zval_ptr_dtor(&args_buffer[i]);
        }
        if (from_pool) {
            sf_pool_release(sf_pool_get_manager(), args_buffer, meta->param_count);
        } else if (args_buffer != args_stack) {
            efree(args_buffer);
        }
    }
    
    /* JIT compile for next time if compilation mode is enabled (optional optimization) */
    if (EXPECTED(c->compilation_enabled) && EXPECTED(!zend_hash_exists(&c->compiled_factories, class_name))) {
        sf_factory *factory = sf_compiler_compile_class(meta, ce, 0);
        if (factory) {
            zend_hash_update_ptr(&c->compiled_factories, class_name, factory);
        }
    }
    
    return SUCCESS;
}
