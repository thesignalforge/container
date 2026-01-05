--TEST--
Container: Singleton scope
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

class SingletonService
{
    public static $instanceCount = 0;
    public $id;
    
    public function __construct()
    {
        self::$instanceCount++;
        $this->id = self::$instanceCount;
    }
}

// Test singleton binding
Container::singleton(SingletonService::class);

$a = Container::make(SingletonService::class);
$b = Container::make(SingletonService::class);

if ($a === $b) {
    echo "Singleton returns same instance\n";
}

if ($a->id === 1 && $b->id === 1) {
    echo "Only one instance created\n";
}

// Test instance binding
$customInstance = new SingletonService();
Container::instance('CustomService', $customInstance);

$c = Container::make('CustomService');
$d = Container::make('CustomService');

if ($c === $customInstance && $d === $customInstance) {
    echo "Instance binding works\n";
}

// Test resolved
if (Container::resolved(SingletonService::class)) {
    echo "resolved() works\n";
}

// Test forgetInstance
Container::forgetInstance(SingletonService::class);
$e = Container::make(SingletonService::class);

if ($e !== $a) {
    echo "forgetInstance works\n";
}


// Test singleton with concrete class
class LoggerInterface {}
class FileLogger extends LoggerInterface { public $type = 'file'; }

Container::singleton(LoggerInterface::class, FileLogger::class);
$logger1 = Container::make(LoggerInterface::class);
$logger2 = Container::make(LoggerInterface::class);

if ($logger1 === $logger2 && $logger1 instanceof FileLogger) {
    echo "Singleton with concrete works\n";
}

// Test singleton with dependencies
class Config { public $env = 'prod'; }
class CachedService {
    public static $count = 0;
    public function __construct(public Config $config) { self::$count++; }
}

Container::singleton(CachedService::class);
$cached1 = Container::make(CachedService::class);
$cached2 = Container::make(CachedService::class);

if ($cached1 === $cached2 && CachedService::$count === 1) {
    echo "Singleton with deps works\n";
}

// Test forgetInstances (plural)
Container::forgetInstances();
if (!Container::resolved(CachedService::class)) {
    echo "forgetInstances works\n";
}

echo "Done\n";

?>
--EXPECT--
Singleton returns same instance
Only one instance created
Instance binding works
resolved() works
forgetInstance works
Singleton with concrete works
Singleton with deps works
forgetInstances works
Done

