--TEST--
Container: Compilation for faster resolution
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Test fixtures
class Logger {
    public function __construct() {}
}

class Database {
    public function __construct() {}
}

class UserService {
    public function __construct(
        public Logger $logger,
        public Database $database
    ) {}
}

class SimpleClass {
    public function __construct() {}
}

// Test 1: isCompiled() returns false initially
echo "Test 1: Initial state\n";
var_dump(Container::isCompiled());

// Test 2: compile() returns number of compiled factories
echo "\nTest 2: Compile bindings\n";
Container::bind(Logger::class, Logger::class);
Container::bind(Database::class, Database::class);
Container::bind(UserService::class, UserService::class);
Container::singleton(SimpleClass::class, SimpleClass::class);

$count = Container::compile();
echo "Compiled: $count factories\n";
var_dump(Container::isCompiled());

// Test 3: Resolution still works after compilation
echo "\nTest 3: Resolution after compilation\n";
$logger = Container::make(Logger::class);
var_dump($logger instanceof Logger);

$userService = Container::make(UserService::class);
var_dump($userService instanceof UserService);
var_dump($userService->logger instanceof Logger);
var_dump($userService->database instanceof Database);

// Test 4: Singletons work with compiled factories
echo "\nTest 4: Singleton behavior\n";
$simple1 = Container::make(SimpleClass::class);
$simple2 = Container::make(SimpleClass::class);
var_dump($simple1 === $simple2);

// Test 5: clearCompiled() disables compilation
echo "\nTest 5: Clear compiled\n";
Container::clearCompiled();
var_dump(Container::isCompiled());

// Test 6: Resolution still works after clearing
echo "\nTest 6: Resolution after clearing\n";
$logger2 = Container::make(Logger::class);
var_dump($logger2 instanceof Logger);

// Test 7: JIT compilation (compile, then resolve new class)
echo "\nTest 7: JIT compilation\n";
Container::flush();

class NewService {
    public function __construct(public Logger $logger) {}
}

Container::bind(Logger::class, Logger::class);
Container::compile();

// First resolution triggers JIT compilation for NewService
$new1 = Container::make(NewService::class);
var_dump($new1 instanceof NewService);
var_dump($new1->logger instanceof Logger);

// Second resolution should use JIT compiled factory
$new2 = Container::make(NewService::class);
var_dump($new2 instanceof NewService);

echo "\nDone!\n";
?>
--EXPECT--
Test 1: Initial state
bool(false)

Test 2: Compile bindings
Compiled: 4 factories
bool(true)

Test 3: Resolution after compilation
bool(true)
bool(true)
bool(true)
bool(true)

Test 4: Singleton behavior
bool(true)

Test 5: Clear compiled
bool(false)

Test 6: Resolution after clearing
bool(true)

Test 7: JIT compilation
bool(true)
bool(true)
bool(true)

Done!
