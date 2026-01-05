--TEST--
Container: Memory safety and stress testing
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Test fixtures
class MemoryService {
    public static int $instances = 0;
    public function __construct() {
        self::$instances++;
    }
}

class MemoryWithDeps {
    public function __construct(public MemoryService $service) {}
}

// Test 1: Resolve many services (memory leak check)
echo "Test 1: Resolve 1000 services\n";
Container::bind(MemoryService::class);

$count = 0;
for ($i = 0; $i < 1000; $i++) {
    $service = Container::make(MemoryService::class);
    if ($service instanceof MemoryService) {
        $count++;
    }
}
var_dump($count === 1000);
var_dump(MemoryService::$instances === 1000); // All created

// Test 2: Singleton reuse (should not create new instances)
echo "\nTest 2: Singleton reuse\n";
Container::flush();
MemoryService::$instances = 0;
Container::singleton(MemoryService::class);

for ($i = 0; $i < 1000; $i++) {
    $service = Container::make(MemoryService::class);
}
var_dump(MemoryService::$instances === 1); // Only one instance

// Test 3: Dependency resolution stress
echo "\nTest 3: Dependency resolution stress\n";
Container::flush();
MemoryService::$instances = 0;
Container::singleton(MemoryService::class);
Container::bind(MemoryWithDeps::class);

$count = 0;
for ($i = 0; $i < 500; $i++) {
    $service = Container::make(MemoryWithDeps::class);
    if ($service->service instanceof MemoryService) {
        $count++;
    }
}
var_dump($count === 500);
var_dump(MemoryService::$instances === 1); // Singleton reused

// Test 4: Flush and recreate many times
echo "\nTest 4: Repeated flush cycles\n";
$flushCount = 0;
for ($i = 0; $i < 100; $i++) {
    Container::flush();
    Container::singleton(MemoryService::class);
    $service = Container::make(MemoryService::class);
    if ($service instanceof MemoryService) {
        $flushCount++;
    }
}
var_dump($flushCount === 100);

// Test 5: Large object graph
echo "\nTest 5: Large object graph\n";
Container::flush();
MemoryService::$instances = 0;

// Create 50 singleton bindings
for ($i = 0; $i < 50; $i++) {
    Container::singleton("Service$i", MemoryService::class);
}

// Resolve all
$resolved = 0;
for ($i = 0; $i < 50; $i++) {
    $service = Container::make("Service$i");
    if ($service instanceof MemoryService) {
        $resolved++;
    }
}
var_dump($resolved === 50);
var_dump(MemoryService::$instances === 50);

// Test 6: Forget instances stress
echo "\nTest 6: Forget instances stress\n";
Container::flush();
Container::singleton(MemoryService::class);

for ($i = 0; $i < 100; $i++) {
    Container::make(MemoryService::class);
    Container::forgetInstance(MemoryService::class);
}
var_dump(Container::resolved(MemoryService::class) === false);

// Test 7: Mixed operations
echo "\nTest 7: Mixed operations\n";
Container::flush();
MemoryService::$instances = 0;

for ($i = 0; $i < 50; $i++) {
    Container::singleton("Singleton$i", MemoryService::class);
    Container::bind("Transient$i", MemoryService::class);
}

$singletonCount = 0;
$transientCount = 0;

for ($i = 0; $i < 50; $i++) {
    $s = Container::make("Singleton$i");
    $t = Container::make("Transient$i");
    if ($s instanceof MemoryService) $singletonCount++;
    if ($t instanceof MemoryService) $transientCount++;
}

var_dump($singletonCount === 50);
var_dump($transientCount === 50);

echo "\nDone!\n";
?>
--EXPECT--
Test 1: Resolve 1000 services
bool(true)
bool(true)

Test 2: Singleton reuse
bool(true)

Test 3: Dependency resolution stress
bool(true)
bool(true)

Test 4: Repeated flush cycles
bool(true)

Test 5: Large object graph
bool(true)
bool(true)

Test 6: Forget instances stress
bool(true)

Test 7: Mixed operations
bool(true)
bool(true)

Done!
