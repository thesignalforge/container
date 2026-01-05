--TEST--
Container: Edge cases and boundary conditions
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Test fixtures
class EdgeSimple {}

class EdgeLevel1 { public function __construct(public EdgeSimple $s) {} }
class EdgeLevel2 { public function __construct(public EdgeLevel1 $s) {} }
class EdgeLevel3 { public function __construct(public EdgeLevel2 $s) {} }
class EdgeLevel4 { public function __construct(public EdgeLevel3 $s) {} }
class EdgeLevel5 { public function __construct(public EdgeLevel4 $s) {} }
class EdgeLevel6 { public function __construct(public EdgeLevel5 $s) {} }
class EdgeLevel7 { public function __construct(public EdgeLevel6 $s) {} }
class EdgeLevel8 { public function __construct(public EdgeLevel7 $s) {} }
class EdgeLevel9 { public function __construct(public EdgeLevel8 $s) {} }
class EdgeLevel10 { public function __construct(public EdgeLevel9 $s) {} }

class EdgeWithDefaults {
    public function __construct(
        public string $required,
        public string $optional = 'default',
        public ?string $nullable = null
    ) {}
}

class EdgeUnicodeService {}
class Edge中文Service {}
class EdgeЯзыкService {}

// Test 1: Very deep dependency tree (10 levels)
echo "Test 1: Deep dependency tree\n";
$deep = Container::make(EdgeLevel10::class);
var_dump($deep instanceof EdgeLevel10);
var_dump($deep->s->s->s->s->s->s->s->s->s->s instanceof EdgeSimple);

// Test 2: Empty container operations
echo "\nTest 2: Empty container operations\n";
Container::flush();
var_dump(Container::bound('NonExistent') === false);
var_dump(Container::resolved('NonExistent') === false);

// Test 3: Constructor with defaults and nullable
echo "\nTest 3: Constructor with defaults\n";
Container::flush();
Container::bind(EdgeWithDefaults::class, function() {
    return new EdgeWithDefaults('test');
});
$obj = Container::make(EdgeWithDefaults::class);
var_dump($obj->required === 'test');
var_dump($obj->optional === 'default');
var_dump($obj->nullable === null);

// Test 4: Unicode class names
echo "\nTest 4: Unicode class names\n";
Container::flush();
Container::singleton(EdgeUnicodeService::class);
Container::singleton(Edge中文Service::class);
Container::singleton(EdgeЯзыкService::class);

$u1 = Container::make(EdgeUnicodeService::class);
$u2 = Container::make(Edge中文Service::class);
$u3 = Container::make(EdgeЯзыкService::class);

var_dump($u1 instanceof EdgeUnicodeService);
var_dump($u2 instanceof Edge中文Service);
var_dump($u3 instanceof EdgeЯзыкService);

// Test 5: Large number of bindings
echo "\nTest 5: Many bindings\n";
Container::flush();
for ($i = 0; $i < 100; $i++) {
    Container::bind("Service$i", EdgeSimple::class);
}
var_dump(Container::bound('Service0'));
var_dump(Container::bound('Service50'));
var_dump(Container::bound('Service99'));
var_dump(Container::bound('Service100') === false);

// Test 6: Repeated flush operations
echo "\nTest 6: Repeated flush\n";
Container::flush();
Container::singleton(EdgeSimple::class);
$s1 = Container::make(EdgeSimple::class);

Container::flush();
Container::singleton(EdgeSimple::class);
$s2 = Container::make(EdgeSimple::class);

var_dump($s1 !== $s2); // Different instances after flush

// Test 7: Alias chains
echo "\nTest 7: Alias chains\n";
Container::flush();
Container::bind(EdgeSimple::class);
Container::alias(EdgeSimple::class, 'alias1');
Container::alias('alias1', 'alias2');
Container::alias('alias2', 'alias3');

$original = Container::make(EdgeSimple::class);
$via_alias = Container::make('alias3');

var_dump($original instanceof EdgeSimple);
var_dump($via_alias instanceof EdgeSimple);

// Test 8: Forget and rebind
echo "\nTest 8: Forget and rebind\n";
Container::flush();
Container::singleton(EdgeSimple::class);
$first = Container::make(EdgeSimple::class);

Container::forgetInstance(EdgeSimple::class);
$second = Container::make(EdgeSimple::class);

var_dump($first !== $second); // Different after forget

// Test 9: Mixed singleton and transient
echo "\nTest 9: Mixed scopes\n";
Container::flush();
Container::singleton(EdgeSimple::class);
Container::bind(EdgeLevel1::class); // Transient

$singleton1 = Container::make(EdgeSimple::class);
$singleton2 = Container::make(EdgeSimple::class);
$transient1 = Container::make(EdgeLevel1::class);
$transient2 = Container::make(EdgeLevel1::class);

var_dump($singleton1 === $singleton2);
var_dump($transient1 !== $transient2);
var_dump($transient1->s === $singleton1); // Transient uses same singleton

echo "\nDone!\n";
?>
--EXPECT--
Test 1: Deep dependency tree
bool(true)
bool(true)

Test 2: Empty container operations
bool(true)
bool(true)

Test 3: Constructor with defaults
bool(true)
bool(true)
bool(true)

Test 4: Unicode class names
bool(true)
bool(true)
bool(true)

Test 5: Many bindings
bool(true)
bool(true)
bool(true)
bool(true)

Test 6: Repeated flush
bool(true)

Test 7: Alias chains
bool(true)
bool(true)

Test 8: Forget and rebind
bool(true)

Test 9: Mixed scopes
bool(true)
bool(true)
bool(true)

Done!
