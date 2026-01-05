--TEST--
Container: Basic binding and resolution
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

// Simple class for testing
class SimpleService
{
    public $value = 'simple';
}

// Test basic binding
Container::bind(SimpleService::class);
$service = Container::make(SimpleService::class);

if ($service instanceof SimpleService) {
    echo "Basic binding works\n";
}

// Test self-binding (no explicit concrete)
Container::bind('TestClass', SimpleService::class);
$service2 = Container::make('TestClass');

if ($service2 instanceof SimpleService) {
    echo "Self-binding works\n";
}

// Test has/bound
if (Container::has(SimpleService::class)) {
    echo "has() works\n";
}

if (Container::bound('TestClass')) {
    echo "bound() works\n";
}

// Test transient (new instance each time)
$a = Container::make(SimpleService::class);
$b = Container::make(SimpleService::class);

if ($a !== $b) {
    echo "Transient binding works\n";
}

// Test bind() with only abstract (self-binding)
class SelfBoundService { public $value = 'self'; }
Container::bind(SelfBoundService::class);
$self = Container::make(SelfBoundService::class);
if ($self instanceof SelfBoundService && $self->value === 'self') {
    echo "Self-bind returns instance\n";
}

// Test bind() with null concrete (should self-bind)
class NullConcreteService { public $value = 'null-concrete'; }
Container::bind(NullConcreteService::class, null);
$nullConcrete = Container::make(NullConcreteService::class);
if ($nullConcrete instanceof NullConcreteService && $nullConcrete->value === 'null-concrete') {
    echo "Null concrete self-binds\n";
}

// Test flush() clears all bindings
Container::flush();
if (!Container::bound(SimpleService::class)) {
    echo "flush() clears bindings\n";
}

echo "Done\n";

?>
--EXPECT--
Basic binding works
Self-binding works
has() works
bound() works
Transient binding works
Self-bind returns instance
Null concrete self-binds
flush() clears bindings
Done

