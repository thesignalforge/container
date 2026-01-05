/*
 * Binary cache file implementation for pre-resolved singleton services.
 */

#include "cache_file.h"
#include "php.h"
#include "ext/standard/php_var.h"
#include "ext/standard/php_string.h"
#include "Zend/zend_hash.h"
#include "Zend/zend_smart_str.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef _WIN32
    #include <io.h>
    #include <direct.h>
    #define access _access
    #define chmod _chmod
    #define S_IRUSR _S_IREAD
    #define S_IWUSR _S_IWRITE
#else
    #include <unistd.h>
#endif

/**
 * Cleanup helper for save operations.
 */
static inline void sf_cache_save_cleanup(FILE *fp, smart_str *buf)
{
    if (buf && buf->s) {
        smart_str_free(buf);
    }
    if (fp) {
        fclose(fp);
    }
}

/**
 * Cleanup helper for load operations.
 */
static inline void sf_cache_load_cleanup(FILE *fp, char *key_buf, char *val_buf)
{
    if (key_buf) efree(key_buf);
    if (val_buf) efree(val_buf);
    if (fp) fclose(fp);
}

/**
 * Generate cache file path based on bindings hash.
 */
zend_string *sf_cache_get_path(HashTable *bindings)
{
    if (!bindings) {
        return NULL;
    }
    
    /* Generate hash of all binding keys */
    zend_ulong hash = 0;
    zend_string *key;
    zval *val;
    
    ZEND_HASH_FOREACH_STR_KEY_VAL(bindings, key, val) {
        if (key) {
            hash = zend_inline_hash_func(ZSTR_VAL(key), ZSTR_LEN(key)) ^ hash;
        }
    } ZEND_HASH_FOREACH_END();
    
    /* Use system temp directory - platform-specific */
    const char *tmpdir;
    
#ifdef _WIN32
    tmpdir = getenv("TEMP");
    if (!tmpdir) tmpdir = getenv("TMP");
    if (!tmpdir) tmpdir = "C:\\Windows\\Temp";
#else
    tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
#endif
    
    /* Format: {tmpdir}/signalforge_cache_<hash>.bin */
    char path[512];
    snprintf(path, sizeof(path), "%s%ssignalforge_cache_%016lx.bin", 
             tmpdir, 
#ifdef _WIN32
             "\\",
#else
             "/",
#endif
             hash);
    
    return zend_string_init(path, strlen(path), 0);
}

/**
 * Check if cache file exists and is valid.
 */
int sf_cache_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return 0;
    }
    
    /* Check if it's a regular file and readable */
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    
    /* Try to open and verify magic number */
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return 0;
    }
    
    uint32_t magic = 0;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != SF_CACHE_MAGIC) {
        fclose(fp);
        return 0;
    }
    
    fclose(fp);
    return 1;
}

/**
 * Save singleton instances to binary cache file.
 */
int sf_cache_save(const char *path, HashTable *instances)
{
    if (!path || !instances) {
        return FAILURE;
    }
    
    /* Don't save if hash table is empty or corrupted */
    if (zend_hash_num_elements(instances) == 0) {
        return SUCCESS;
    }
    
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        php_error_docref(NULL, E_WARNING, 
            "Failed to open cache file for writing: %s (error: %s)", 
            path, strerror(errno));
        return FAILURE;
    }
    
    /* Count objects only */
    uint32_t count = 0;
    zend_string *key;
    zval *val;
    ZEND_HASH_FOREACH_STR_KEY_VAL(instances, key, val) {
        if (key && val && Z_TYPE_P(val) == IS_OBJECT) {
            count++;
        }
    } ZEND_HASH_FOREACH_END();
    
    /* Write header */
    uint32_t magic = SF_CACHE_MAGIC;
    uint32_t version = SF_CACHE_VERSION;
    
    if (fwrite(&magic, sizeof(magic), 1, fp) != 1 ||
        fwrite(&version, sizeof(version), 1, fp) != 1 ||
        fwrite(&count, sizeof(count), 1, fp) != 1) {
        php_error_docref(NULL, E_WARNING, 
            "Failed to write cache header to: %s", path);
        sf_cache_save_cleanup(fp, NULL);
        return FAILURE;
    }
    
    /* Write each service */
    ZEND_HASH_FOREACH_STR_KEY_VAL(instances, key, val) {
        if (!key || !val) continue;
        
        /* Skip if value is not an object (safety check) */
        if (Z_TYPE_P(val) != IS_OBJECT) {
            continue;
        }
        
        /* Write key */
        uint32_t key_len = ZSTR_LEN(key);
        if (fwrite(&key_len, sizeof(key_len), 1, fp) != 1 ||
            fwrite(ZSTR_VAL(key), key_len, 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to write service key to cache: %s", ZSTR_VAL(key));
            sf_cache_save_cleanup(fp, NULL);
            return FAILURE;
        }
        
        /* Serialize value using PHP's serialize() */
        php_serialize_data_t var_hash;
        smart_str buf = {0};
        
        PHP_VAR_SERIALIZE_INIT(var_hash);
        php_var_serialize(&buf, val, &var_hash);
        PHP_VAR_SERIALIZE_DESTROY(var_hash);
        
        if (!buf.s || ZSTR_LEN(buf.s) == 0) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to serialize service: %s", ZSTR_VAL(key));
            sf_cache_save_cleanup(fp, &buf);
            return FAILURE;
        }
        
        /* Write serialized value */
        uint32_t val_len = ZSTR_LEN(buf.s);
        if (fwrite(&val_len, sizeof(val_len), 1, fp) != 1 ||
            fwrite(ZSTR_VAL(buf.s), val_len, 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to write serialized service to cache: %s", ZSTR_VAL(key));
            sf_cache_save_cleanup(fp, &buf);
            return FAILURE;
        }
        
        smart_str_free(&buf);
    } ZEND_HASH_FOREACH_END();
    
    fclose(fp);
    
    /* Set permissions to 0600 (owner read/write only) */
#ifdef _WIN32
    /* On Windows, use _chmod with _S_IREAD | _S_IWRITE */
    if (chmod(path, S_IRUSR | S_IWUSR) != 0) {
        php_error_docref(NULL, E_WARNING, 
            "Failed to set cache file permissions: %s", path);
    }
#else
    /* On Unix-like systems, use chmod with 0600 */
    if (chmod(path, 0600) != 0) {
        php_error_docref(NULL, E_WARNING, 
            "Failed to set cache file permissions: %s", path);
    }
#endif
    
    return SUCCESS;
}

/**
 * Load singleton instances from binary cache file.
 */
int sf_cache_load(const char *path, HashTable *instances)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        php_error_docref(NULL, E_WARNING, 
            "Failed to open cache file for reading: %s (error: %s)", 
            path, strerror(errno));
        return FAILURE;
    }
    
    /* Read and verify header */
    uint32_t magic, version, count;
    
    if (fread(&magic, sizeof(magic), 1, fp) != 1) {
        php_error_docref(NULL, E_WARNING, "Failed to read cache magic number");
        sf_cache_load_cleanup(fp, NULL, NULL);
        return FAILURE;
    }
    
    if (magic != SF_CACHE_MAGIC) {
        php_error_docref(NULL, E_WARNING, 
            "Invalid cache file magic number: 0x%08x (expected 0x%08x)", 
            magic, SF_CACHE_MAGIC);
        sf_cache_load_cleanup(fp, NULL, NULL);
        return FAILURE;
    }
    
    if (fread(&version, sizeof(version), 1, fp) != 1) {
        php_error_docref(NULL, E_WARNING, "Failed to read cache version");
        sf_cache_load_cleanup(fp, NULL, NULL);
        return FAILURE;
    }
    
    if (version != SF_CACHE_VERSION) {
        php_error_docref(NULL, E_WARNING, 
            "Incompatible cache version: %u (expected %u)", 
            version, SF_CACHE_VERSION);
        sf_cache_load_cleanup(fp, NULL, NULL);
        return FAILURE;
    }
    
    if (fread(&count, sizeof(count), 1, fp) != 1) {
        php_error_docref(NULL, E_WARNING, "Failed to read service count");
        sf_cache_load_cleanup(fp, NULL, NULL);
        return FAILURE;
    }
    
    /* Read each service */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t key_len;
        if (fread(&key_len, sizeof(key_len), 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to read key length for service %u", i);
            sf_cache_load_cleanup(fp, NULL, NULL);
            return FAILURE;
        }
        
        /* Read key */
        char *key_buf = emalloc(key_len + 1);
        if (fread(key_buf, key_len, 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to read key data for service %u", i);
            sf_cache_load_cleanup(fp, key_buf, NULL);
            return FAILURE;
        }
        key_buf[key_len] = '\0';
        
        /* Read serialized value length */
        uint32_t val_len;
        if (fread(&val_len, sizeof(val_len), 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to read value length for service: %s", key_buf);
            sf_cache_load_cleanup(fp, key_buf, NULL);
            return FAILURE;
        }
        
        /* Read serialized value */
        char *val_buf = emalloc(val_len + 1);
        if (fread(val_buf, val_len, 1, fp) != 1) {
            php_error_docref(NULL, E_WARNING, 
                "Failed to read value data for service: %s", key_buf);
            sf_cache_load_cleanup(fp, key_buf, val_buf);
            return FAILURE;
        }
        val_buf[val_len] = '\0';
        
        /* Unserialize value */
        zval unserialized;
        php_unserialize_data_t var_hash;
        const unsigned char *p = (const unsigned char *)val_buf;
        
        PHP_VAR_UNSERIALIZE_INIT(var_hash);
        if (!php_var_unserialize(&unserialized, &p, p + val_len, &var_hash)) {
            PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
            php_error_docref(NULL, E_WARNING, 
                "Failed to unserialize service: %s", key_buf);
            sf_cache_load_cleanup(fp, key_buf, val_buf);
            return FAILURE;
        }
        PHP_VAR_UNSERIALIZE_DESTROY(var_hash);
        
        /* Add to instances hash table */
        zend_string *key_str = zend_string_init(key_buf, key_len, 0);
        zend_hash_add(instances, key_str, &unserialized);
        zend_string_release(key_str);
        
        efree(key_buf);
        efree(val_buf);
    }
    
    fclose(fp);
    return SUCCESS;
}
