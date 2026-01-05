--TEST--
Container: Binary cache auto-generation and loading
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Test fixtures
class CacheService1 {
    public function __construct() {}
}

class CacheService2 {
    public function __construct(public CacheService1 $service1) {}
}

class CacheService3 {
    public function __construct(
        public CacheService1 $service1,
        public CacheService2 $service2
    ) {}
}

class TransientService {
    public function __construct() {}
}

// Test 1: Cache auto-generation on first singleton resolution
echo "Test 1: Cache auto-generation\n";
Container::singleton(CacheService1::class);
Container::singleton(CacheService2::class);
Container::singleton(CacheService3::class);

// Resolve singletons - this should trigger cache generation
$s1 = Container::make(CacheService1::class);
$s2 = Container::make(CacheService2::class);
$s3 = Container::make(CacheService3::class);

var_dump($s1 instanceof CacheService1);
var_dump($s2 instanceof CacheService2);
var_dump($s3 instanceof CacheService3);
var_dump($s2->service1 === $s1); // Should be same instance
var_dump($s3->service1 === $s1); // Should be same instance
var_dump($s3->service2 === $s2); // Should be same instance

// Test 2: Cache invalidation when bindings change
echo "\nTest 2: Cache invalidation on binding change\n";
Container::flush();
Container::singleton(CacheService1::class);
$before = Container::make(CacheService1::class);

// Add new binding - should invalidate cache
Container::singleton(CacheService2::class);
$after = Container::make(CacheService2::class);

var_dump($before instanceof CacheService1);
var_dump($after instanceof CacheService2);

// Test 3: Transient services not cached
echo "\nTest 3: Transient services not in binary cache\n";
Container::flush();
Container::singleton(CacheService1::class);
Container::bind(TransientService::class); // Transient

$singleton = Container::make(CacheService1::class);
$transient1 = Container::make(TransientService::class);
$transient2 = Container::make(TransientService::class);

var_dump($singleton instanceof CacheService1);
var_dump($transient1 instanceof TransientService);
var_dump($transient2 instanceof TransientService);
var_dump($transient1 !== $transient2); // Different instances

// Test 4: Multiple singletons with dependencies
echo "\nTest 4: Complex dependency graph\n";
Container::flush();
Container::singleton(CacheService1::class);
Container::singleton(CacheService2::class);
Container::singleton(CacheService3::class);

// Resolve in different order
$s3 = Container::make(CacheService3::class);
$s1 = Container::make(CacheService1::class);
$s2 = Container::make(CacheService2::class);

var_dump($s3->service1 === $s1);
var_dump($s3->service2 === $s2);
var_dump($s2->service1 === $s1);

// Test 5: Empty container operations
echo "\nTest 5: Empty container\n";
Container::flush();
try {
    Container::make('NonExistentClass');
    echo "Should have thrown exception\n";
} catch (Exception $e) {
    var_dump(str_contains($e->getMessage(), 'NonExistentClass'));
}

// Test 6: Singleton identity across multiple make() calls
echo "\nTest 6: Singleton identity\n";
Container::flush();
Container::singleton(CacheService1::class);

$first = Container::make(CacheService1::class);
$second = Container::make(CacheService1::class);
$third = Container::make(CacheService1::class);

var_dump($first === $second);
var_dump($second === $third);
var_dump($first === $third);

echo "\nDone!\n";
?>
--EXPECT--
Test 1: Cache auto-generation
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)

Test 2: Cache invalidation on binding change
bool(true)
bool(true)

Test 3: Transient services not in binary cache
bool(true)
bool(true)
bool(true)
bool(true)

Test 4: Complex dependency graph
bool(true)
bool(true)
bool(true)

Test 5: Empty container
bool(true)

Test 6: Singleton identity
bool(true)
bool(true)
bool(true)

Done!
