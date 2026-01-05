--TEST--
Container: PHP code generation (dump/loadCompiled)
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Test fixtures
class DumpLogger {
    public function __construct() {}
}

class DumpDatabase {
    public function __construct() {}
}

class DumpUserService {
    public function __construct(
        public DumpLogger $logger,
        public DumpDatabase $database
    ) {}
}

class DumpSimpleClass {
    public function __construct() {}
}

// Test 1: hasCompiled() returns false initially
echo "Test 1: Initial state\n";
var_dump(Container::hasCompiled());

// Test 2: getBindings() returns registered bindings
echo "\nTest 2: getBindings()\n";
Container::bind(DumpLogger::class, DumpLogger::class);
Container::singleton(DumpDatabase::class, DumpDatabase::class);
Container::bind(DumpUserService::class, DumpUserService::class);

$bindings = Container::getBindings();
var_dump(count($bindings) >= 3);
var_dump(isset($bindings[DumpLogger::class]));
var_dump($bindings[DumpLogger::class]['scope'] === 'transient');
var_dump($bindings[DumpDatabase::class]['scope'] === 'singleton');

// Test 3: getMetadata() returns class info
echo "\nTest 3: getMetadata()\n";
$meta = Container::getMetadata(DumpUserService::class);
var_dump($meta !== null);
var_dump($meta['instantiable']);
var_dump($meta['paramCount'] === 2);
var_dump($meta['params'][0]['name'] === 'logger');
var_dump($meta['params'][0]['type'] === DumpLogger::class);

// Test 4: getMetadata() returns null for non-existent class
echo "\nTest 4: getMetadata() for non-existent\n";
$meta = Container::getMetadata('NonExistentClass');
var_dump($meta === null);

// Test 5: unloadCompiled() works when nothing loaded
echo "\nTest 5: unloadCompiled() safe when empty\n";
Container::unloadCompiled();
var_dump(Container::hasCompiled() === false);

// Note: Full dump/loadCompiled tests require container-php package
// which provides ContainerDumper and CompiledContainer classes

echo "\nDone!\n";
?>
--EXPECT--
Test 1: Initial state
bool(false)

Test 2: getBindings()
bool(true)
bool(true)
bool(true)
bool(true)

Test 3: getMetadata()
bool(true)
bool(true)
bool(true)
bool(true)
bool(true)

Test 4: getMetadata() for non-existent
bool(true)

Test 5: unloadCompiled() safe when empty
bool(true)

Done!
