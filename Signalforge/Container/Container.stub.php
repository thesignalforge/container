<?php
/**
 * Signalforge Container Extension
 * Container.stub.php - IDE stub for Container class
 *
 * @package Signalforge\Container
 */

namespace Signalforge\Container;

/**
 * High-performance Dependency Injection Container.
 *
 * The Container class provides a static interface for dependency injection
 * with support for autowiring, singletons, contextual bindings, and tagging.
 * All resolutions are performed in native C for maximum performance.
 *
 * @final
 */
final class Container
{
    /**
     * Register a binding with the container.
     *
     * @param string $abstract The abstract type (interface or class name)
     * @param mixed $concrete The concrete implementation (class name, closure, or null for self-binding)
     * @return void
     */
    public static function bind(string $abstract, mixed $concrete = null): void {}

    /**
     * Register a singleton binding with the container.
     * The resolved instance will be cached and reused.
     *
     * @param string $abstract The abstract type
     * @param mixed $concrete The concrete implementation
     * @return void
     */
    public static function singleton(string $abstract, mixed $concrete = null): void {}

    /**
     * Register an existing instance as a singleton.
     *
     * @param string $abstract The abstract type
     * @param object $instance The object instance
     * @return void
     */
    public static function instance(string $abstract, object $instance): void {}

    /**
     * Resolve a type from the container.
     *
     * @param string $abstract The abstract type to resolve
     * @param array $parameters Optional parameters to pass to constructor
     * @return mixed The resolved instance
     * @throws NotFoundException If the type cannot be resolved
     * @throws CircularDependencyException If a circular dependency is detected
     */
    public static function make(string $abstract, array $parameters = []): mixed {}

    /**
     * Resolve a type from the container (PSR-11 compatible).
     *
     * @param string $id The identifier
     * @return mixed The resolved instance
     * @throws NotFoundException If the type cannot be resolved
     */
    public static function get(string $id): mixed {}

    /**
     * Check if the container can resolve a type (PSR-11 compatible).
     *
     * @param string $id The identifier
     * @return bool True if resolvable
     */
    public static function has(string $id): bool {}

    /**
     * Check if a type has been bound to the container.
     *
     * @param string $abstract The abstract type
     * @return bool True if bound
     */
    public static function bound(string $abstract): bool {}

    /**
     * Check if a type has been resolved.
     *
     * @param string $abstract The abstract type
     * @return bool True if already resolved
     */
    public static function resolved(string $abstract): bool {}

    /**
     * Create an alias for an abstract type.
     *
     * @param string $abstract The abstract type
     * @param string $alias The alias name
     * @return void
     */
    public static function alias(string $abstract, string $alias): void {}

    /**
     * Tag a set of bindings with a name.
     *
     * @param array $abstracts Array of abstract types
     * @param string $tag The tag name
     * @return void
     */
    public static function tag(array $abstracts, string $tag): void {}

    /**
     * Resolve all bindings for a given tag.
     *
     * @param string $tag The tag name
     * @return array Array of resolved instances
     */
    public static function tagged(string $tag): array {}

    /**
     * Define a contextual binding.
     *
     * Usage:
     * ```php
     * Container::when(UserController::class)
     *     ->needs(LoggerInterface::class)
     *     ->give(FileLogger::class);
     * ```
     *
     * @param string $concrete The class that has the dependency
     * @return ContextualBuilder
     */
    public static function when(string $concrete): ContextualBuilder {}

    /**
     * Flush all bindings and resolved instances.
     *
     * @return void
     */
    public static function flush(): void {}

    /**
     * Forget a resolved singleton instance.
     *
     * @param string $abstract The abstract type
     * @return void
     */
    public static function forgetInstance(string $abstract): void {}

    /**
     * Forget all resolved singleton instances.
     *
     * @return void
     */
    public static function forgetInstances(): void {}

    /**
     * Compile all registered bindings for faster resolution.
     *
     * Call this in production after registering all bindings to enable
     * compiled factories. Compiled resolution is ~3x faster than autowiring.
     *
     * @return int Number of factories compiled
     */
    public static function compile(): int {}

    /**
     * Check if the container is in compiled mode.
     *
     * @return bool True if compile() has been called
     */
    public static function isCompiled(): bool {}

    /**
     * Clear compiled factories and disable compiled mode.
     *
     * @return void
     */
    public static function clearCompiled(): void {}

    /**
     * Get all registered bindings for code generation.
     *
     * Returns an array of binding metadata including abstract, concrete, scope.
     * Used by ContainerDumper to generate optimized PHP code.
     *
     * @return array<string, array{abstract: string, concrete: mixed, scope: string, resolved: bool}>
     */
    public static function getBindings(): array {}

    /**
     * Get reflection metadata for a class.
     *
     * Returns constructor parameter information for code generation.
     *
     * @param string $className The fully qualified class name
     * @return array{class: string, instantiable: bool, paramCount: int, params: array}|null
     */
    public static function getMetadata(string $className): ?array {}

    /**
     * Generate a compiled container PHP file.
     *
     * Creates an optimized PHP class with factory methods for all registered
     * bindings. The generated code is cached by OPcache for maximum performance.
     *
     * @param string $path File path to save the generated container
     * @param string $className Name of the generated class (default: CompiledContainer)
     * @param string $namespace Namespace for the generated class (default: none)
     * @param bool $eager Enable eager singleton pre-resolution for maximum performance (default: false)
     * @return bool True on success
     */
    public static function dump(string $path, string $className = 'CompiledContainer', string $namespace = '', bool $eager = false): bool {}

    /**
     * Load and activate a compiled container.
     *
     * Once loaded, Container::make() will delegate to the compiled container
     * for services it knows about, falling back to native resolution for others.
     *
     * @param string $path Path to the compiled container PHP file
     * @return bool True on success
     */
    public static function loadCompiled(string $path): bool {}

    /**
     * Unload and deactivate the compiled container.
     *
     * After calling this, Container::make() will use native resolution.
     *
     * @return void
     */
    public static function unloadCompiled(): void {}

    /**
     * Check if a compiled container is currently loaded.
     *
     * @return bool True if a compiled container is active
     */
    public static function hasCompiled(): bool {}

    /**
     * Clear the binary cache file.
     *
     * Deletes the cached singleton instances from disk. The cache will be
     * regenerated on the next request when singletons are resolved.
     *
     * @return bool True on success, false on failure
     */
    public static function clearCache(): bool {}

    /**
     * Get the path to the binary cache file.
     *
     * Returns the filesystem path where singleton instances are cached.
     * The path is based on a hash of the registered bindings.
     *
     * @return string The cache file path
     */
    public static function getCachePath(): string {}
}

