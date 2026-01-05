/*
 * Binary cache file format for pre-resolved singleton services.
 * 
 * This provides instant loading of warmed-up services without serialization overhead.
 * The cache is automatically generated on first request and loaded on subsequent requests.
 * 
 * File format:
 * - Magic number (4 bytes): "SFCN" (SignalForge Container)
 * - Version (4 bytes): Cache format version
 * - Service count (4 bytes): Number of cached services
 * - For each service:
 *   - Key length (4 bytes)
 *   - Key data (variable)
 *   - Serialized zval (PHP serialize format)
 *   - Value length (4 bytes)
 *   - Value data (variable)
 */

#ifndef SF_CACHE_FILE_H
#define SF_CACHE_FILE_H

#include "php.h"

#define SF_CACHE_MAGIC 0x4E434653  /* "SFCN" in little-endian */
#define SF_CACHE_VERSION 1

/**
 * Save singleton instances to binary cache file.
 * 
 * @param path Cache file path
 * @param instances HashTable of singleton instances
 * @return SUCCESS or FAILURE
 */
int sf_cache_save(const char *path, HashTable *instances);

/**
 * Load singleton instances from binary cache file.
 * 
 * @param path Cache file path
 * @param instances HashTable to populate with loaded instances
 * @return SUCCESS or FAILURE
 */
int sf_cache_load(const char *path, HashTable *instances);

/**
 * Check if cache file exists and is valid.
 * 
 * @param path Cache file path
 * @return 1 if valid, 0 otherwise
 */
int sf_cache_exists(const char *path);

/**
 * Generate cache file path based on bindings hash.
 * 
 * @param bindings HashTable of bindings
 * @return zend_string* with cache file path (caller must release)
 */
zend_string *sf_cache_get_path(HashTable *bindings);

#endif /* SF_CACHE_FILE_H */
