# Signalforge Container

A native PHP extension implementing a high-performance Dependency Injection Container with autowiring, singletons, and contextual bindings.

## What's Different

- **Native C implementation** - all container operations run in native code
- **SIMD-accelerated lookups** - uses SSE2/NEON for parallel hash comparisons
- **Swiss Table-inspired cache** - ultra-fast singleton lookup with control bytes
- **Object pooling** - reuses memory buffers to reduce allocation overhead
- **Reflection caching** - constructor metadata is cached to avoid repeated reflection
- **Autowiring by default** - automatic dependency resolution via type hints
- **Zero configuration** - works out of the box with minimal setup
- **PSR-11 compatible** - implements `get()` and `has()` methods
- **Native compilation** - pre-generate native C factory functions for faster resolution

## Why C?

DI containers are invoked on nearly every request, often hundreds of times. Moving container operations to native code provides:

- **Faster resolution** - no userland function calls or array lookups
- **SIMD acceleration** - parallel hash comparisons using SSE2/NEON instructions
- **Memory efficiency** - native hash tables and structs instead of PHP arrays
- **Cache-optimized structures** - 64-byte aligned structs for CPU cache efficiency
- **Object pooling** - reuses memory buffers to eliminate allocation overhead
- **Reflection caching** - constructor metadata cached in native memory
- **Reduced overhead** - minimal PHP engine interaction during resolution

## Requirements

- PHP 8.3+
- Linux or macOS (tested on x86_64 and ARM64/Apple Silicon)
- PHP dev headers (`apt install php-dev` on Linux, included with Xcode on macOS)
- CPU with SSE2 (x86_64) or NEON (ARM64) for SIMD optimizations (automatic fallback to scalar code)

## Building

### Docker (Recommended)

No need to install PHP dev headers on your host:

```bash
cd container

# Build Docker image with extension
make docker-build

# Run tests
make docker-test

# Run example
make docker-example
```

### Host Installation

```bash
cd container
phpize
./configure --enable-signalforge-container
make
make test
sudo make install
```

Then add `extension=signalforge_container.so` to your php.ini.

See [INSTALL.md](INSTALL.md) for detailed instructions.

## Usage

### Basic Binding

```php
use Signalforge\Container\Container;

// Bind interface to implementation
Container::bind(LoggerInterface::class, FileLogger::class);

// Resolve
$logger = Container::make(LoggerInterface::class);
```

### Autowiring

The container automatically resolves dependencies via type hints:

```php
class UserService
{
    public function __construct(
        Logger $logger,
        Database $database
    ) {
        // Dependencies injected automatically
    }
}

// No explicit bindings needed
$service = Container::make(UserService::class);
```

### Singletons

```php
// Bind as singleton
Container::singleton(Database::class);

$db1 = Container::make(Database::class);
$db2 = Container::make(Database::class);

assert($db1 === $db2); // Same instance
```

### Existing Instances

```php
$config = new Config(['env' => 'production']);
Container::instance(Config::class, $config);

$resolved = Container::make(Config::class);
assert($resolved === $config);
```

### Closures

```php
Container::bind('database', function($container, $params) {
    return new Database(
        $params['host'] ?? 'localhost',
        $params['port'] ?? 3306
    );
});

$db = Container::make('database', [
    'host' => 'mysql.example.com',
    'port' => 3307
]);
```

### Contextual Bindings

Different implementations based on context:

```php
// UserController gets FileLogger
Container::when(UserController::class)
    ->needs(LoggerInterface::class)
    ->give(FileLogger::class);

// AdminController gets DatabaseLogger
Container::when(AdminController::class)
    ->needs(LoggerInterface::class)
    ->give(DatabaseLogger::class);
```

### Tagging

Group related bindings:

```php
Container::tag([
    EmailNotifier::class,
    SmsNotifier::class,
    PushNotifier::class
], 'notifiers');

// Resolve all tagged
$notifiers = Container::tagged('notifiers');
foreach ($notifiers as $notifier) {
    $notifier->send($message);
}
```

### Aliases

```php
Container::alias(LoggerInterface::class, 'logger');

$logger = Container::make('logger');
```

### Parameter Override

```php
$service = Container::make(UserService::class, [
    'logger' => new CustomLogger()
]);
```

### Compilation (Production Optimization)

Pre-generate native C factory functions for ~25% faster resolution:

```php
// Register all bindings
Container::bind(LoggerInterface::class, FileLogger::class);
Container::singleton(Database::class);

// Compile to native factories
$count = Container::compile();
echo "Compiled $count factories\n";

// Resolutions now use compiled factories
$service = Container::make(UserService::class);
```

## API Reference

### Binding

```php
// Transient binding (new instance each time)
Container::bind(string $abstract, mixed $concrete = null): void

// Singleton binding (cached instance)
Container::singleton(string $abstract, mixed $concrete = null): void

// Existing instance
Container::instance(string $abstract, object $instance): void
```

### Resolution

```php
// Resolve with optional parameters
Container::make(string $abstract, array $parameters = []): mixed

// PSR-11 get
Container::get(string $id): mixed

// PSR-11 has
Container::has(string $id): bool
```

### Introspection

```php
// Check if bound
Container::bound(string $abstract): bool

// Check if resolved (singleton cached)
Container::resolved(string $abstract): bool
```

### Contextual Bindings

```php
Container::when(string $concrete): ContextualBuilder

// Example:
Container::when(UserController::class)
    ->needs(LoggerInterface::class)
    ->give(FileLogger::class);
```

### Tagging

```php
Container::tag(array $abstracts, string $tag): void
Container::tagged(string $tag): array
```

### Aliases

```php
Container::alias(string $abstract, string $alias): void
```

### Lifecycle

```php
// Clear all bindings and instances
Container::flush(): void

// Forget a specific singleton instance
Container::forgetInstance(string $abstract): void

// Forget all singleton instances
Container::forgetInstances(): void
```

### Compilation

```php
// Compile all bindings for faster resolution
Container::compile(): int

// Check if compilation is enabled
Container::isCompiled(): bool

// Clear compiled factories
Container::clearCompiled(): void
```

## How It Works

### Reflection Caching

When resolving a class for the first time, the container uses PHP's reflection API to inspect the constructor parameters. This metadata is cached in a native hash table to avoid repeated reflection calls:

```c
typedef struct {
    zend_string *class_name;
    uint32_t param_count;
    sf_param_info *params;  // Array of parameter info
    zend_bool is_instantiable;
} sf_class_meta;
```

### Resolution Process

1. **SIMD fast lookup** - check Swiss Table cache with parallel hash comparison (NEON/SSE2)
2. **Check for circular dependency** - SIMD-accelerated stack search (4 hashes at once)
3. **Check for existing singleton** - return cached if exists
4. **Check compiled factory** - use pre-generated native factory if available
5. **Check for contextual binding** - use context-specific implementation
6. **Check for explicit binding** - use registered concrete
7. **Autowire** - analyze constructor and resolve dependencies (with object pooling)
8. **Cache singleton** - store in both HashTable and fast lookup cache

### Autowiring

The autowiring system:
1. Gets or builds class metadata (cached)
2. Iterates through constructor parameters
3. For each parameter with a type hint:
   - Try to resolve from container
   - Fall back to default value if available
   - Throw exception if required parameter cannot be resolved

## Circular Dependency Detection

The container tracks the resolution stack and throws `CircularDependencyException` when a circular dependency is detected:

```php
class A { public function __construct(B $b) {} }
class B { public function __construct(A $a) {} }

try {
    Container::make(A::class);
} catch (CircularDependencyException $e) {
    echo $e->getMessage(); // "Circular dependency detected: A -> B -> A"
}
```

## Exception Handling

- `ContainerException` - Base exception
- `NotFoundException` - Type cannot be resolved
- `CircularDependencyException` - Circular dependency detected

## Thread Safety

The extension supports ZTS (Zend Thread Safety) builds. Each request gets an isolated container instance, and all operations are thread-safe.

## Performance

Compared to userland implementations like Laravel's Container:

- **13x faster** for simple resolution vs Laravel
- **18x faster** for autowiring vs Laravel
- **Lower memory usage** due to native data structures and object pooling
- **Reflection caching** eliminates repeated `ReflectionClass` instantiation
- **SIMD acceleration** provides 2-4x speedup on circular dependency checks
- **Swiss Table cache** reduces singleton lookup from ~43ns to ~20-25ns
- **Binary cache** provides instant singleton loading at **63ns** (2.6x faster than Symfony!)

### Benchmark Results

| Scenario | Signalforge | SF Compiled | SF Binary Cache | Laravel | Symfony |
|----------|-------------|-------------|-----------------|---------|---------|
| Simple resolution | 89ns | 60ns | - | 1.19μs | 161ns |
| Singleton | 43ns | ~20ns | - | 259ns | 167ns |
| Autowiring (2 deps) | 268ns | 170ns | - | 4.97μs | 174ns |
| Deep deps (5 levels) | 2.44μs | 1.5μs | - | 43.86μs | - |
| **Large app (50+ services, warmed)** | **353ns** | **358ns** | **63ns** ⭐ | **259ns** | **163ns** |
| Circular check (10 deep) | ~80ns | ~25ns | - | - | - |

### Performance Strategy Comparison

| Method | Performance | Setup | Best For |
|--------|-------------|-------|----------|
| No compilation | ~200-350ns | None | Development |
| `compile()` | ~170ns | One-liner | Quick production wins |
| Binary cache (warmed) | **~63ns** ⭐ | Automatic | **Maximum performance** |

**Note:** The "Large app (warmed)" benchmark represents the realistic production scenario where singletons are resolved once and cached. The binary cache provides instant loading on subsequent requests.

### SIMD Optimizations

The extension automatically detects and uses SIMD instructions when available:

- **x86_64**: Uses SSE2 (universally available since 2003)
- **ARM64**: Uses NEON (Apple Silicon, ARM servers)
- **Fallback**: Automatic scalar implementation for unsupported platforms

SIMD provides:
- **4x faster** circular dependency detection (compares 4 hashes in parallel)
- **~40% faster** singleton lookups via Swiss Table control bytes
- **Zero configuration** - automatically enabled when CPU supports it

### Binary Cache (Automatic)

The container automatically caches pre-resolved singleton instances to disk for instant loading on subsequent requests:

- **Automatic generation**: Cache is created when singletons are first resolved
- **Automatic loading**: Cache is loaded at container initialization if it exists
- **63ns lookups**: Faster than Symfony's 163ns (2.6x improvement)
- **Zero configuration**: Works out of the box
- **Cache invalidation**: Automatic when bindings change (based on hash)

```php
// First request: singletons resolved and cached
Container::singleton(Database::class);
$db = Container::make(Database::class); // Resolved, cached to disk

// Subsequent requests: instant loading
$db = Container::make(Database::class); // Loaded from cache (63ns)
```

**Cache Management:**

```php
// Get cache file path
$path = Container::getCachePath();

// Clear cache manually
Container::clearCache();
```

Use `Container::compile()` for easy optimization, or rely on the automatic binary cache for maximum performance.

## Structure

```
container/
├── config.m4                    # Build configuration
├── signalforge_container.c      # PHP class implementations
├── php_signalforge_container.h  # Main header
├── src/
│   ├── container.c/h            # Core container logic
│   ├── binding.c/h              # Binding management
│   ├── autowire.c/h             # Autowiring system
│   ├── reflection_cache.c/h     # Reflection metadata cache
│   ├── cache_file.c/h           # Binary cache for singletons
│   ├── factory.c/h              # Compiled factory structures
│   ├── compiler.c/h             # Native C factory compilation
│   ├── simd.h                   # SIMD intrinsics abstraction (SSE2/NEON)
│   ├── fast_lookup.c/h          # Swiss Table-inspired fast cache
│   └── pool.c/h                 # Object pooling for memory buffers
├── Signalforge/Container/       # IDE stubs
├── examples/                    # Usage examples
└── tests/                       # phpt tests (12 test files)
```

## Examples

See the `examples/` directory for complete usage examples.

## Testing

```bash
make test
```

Or run specific tests:

```bash
php run-tests.php tests/001-basic.phpt
```

## License

MIT License - see [LICENSE](LICENSE) for details.


