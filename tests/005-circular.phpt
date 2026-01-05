--TEST--
Container: Circular dependency detection
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;
use Signalforge\Container\CircularDependencyException;

class ServiceA {
    public function __construct(public ServiceB $b) {}
}

class ServiceB {
    public function __construct(public ServiceC $c) {}
}

class ServiceC {
    public function __construct(public ServiceA $a) {}
}

// Test circular dependency detection
try {
    Container::make(ServiceA::class);
    echo "Should have thrown exception\n";
} catch (CircularDependencyException $e) {
    echo "Circular dependency detected\n";
    if (str_contains($e->getMessage(), 'ServiceA')) {
        echo "Exception message contains ServiceA\n";
    }
}


// Test self-circular dependency
class SelfCircular {
    public function __construct(public SelfCircular $self) {}
}

try {
    Container::make(SelfCircular::class);
    echo "Should have thrown exception\n";
} catch (CircularDependencyException $e) {
    echo "Self-circular dependency detected\n";
}

echo "Done\n";

?>
--EXPECT--
Circular dependency detected
Exception message contains ServiceA
Self-circular dependency detected
Done
