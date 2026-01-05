--TEST--
Container: Closures and aliases
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

class Config
{
    public $env = 'production';
}

// Test closure binding
Container::bind('config', function($container, $params) {
    $config = new Config();
    if (isset($params['env'])) {
        $config->env = $params['env'];
    }
    return $config;
});

$config1 = Container::make('config');
if ($config1->env === 'production') {
    echo "Closure binding works\n";
}

$config2 = Container::make('config', ['env' => 'testing']);
if ($config2->env === 'testing') {
    echo "Closure with parameters works\n";
}

// Test alias
Container::alias('config', 'app.config');
$config3 = Container::make('app.config');

if ($config3 instanceof Config) {
    echo "Alias works\n";
}

// Test has with alias
if (Container::has('app.config')) {
    echo "has() works with alias\n";
}


// Test alias to class binding
class Logger { public $name = 'logger'; }
Container::bind(Logger::class);
Container::alias(Logger::class, 'log');
Container::alias(Logger::class, 'app.log');

$log1 = Container::make('log');
$log2 = Container::make('app.log');
if ($log1 instanceof Logger && $log2 instanceof Logger) {
    echo "Multiple aliases work\n";
}

// Test bound() with alias
if (Container::bound('log') && Container::bound('app.log')) {
    echo "bound() works with alias\n";
}

// Test singleton closure
$callCount = 0;
Container::singleton('counter', function() use (&$callCount) {
    $callCount++;
    return new stdClass();
});

$c1 = Container::make('counter');
$c2 = Container::make('counter');
if ($c1 === $c2 && $callCount === 1) {
    echo "Singleton closure works\n";
}

// Test closure returning scalar
Container::bind('version', fn() => '1.0.0');
if (Container::make('version') === '1.0.0') {
    echo "Closure returning scalar works\n";
}

// Test closure with container access
class Database { public $host = 'localhost'; }
Container::bind(Database::class);
Container::bind('db-info', function($container) {
    $db = $container->make(Database::class);
    return 'Connected to: ' . $db->host;
});
if (Container::make('db-info') === 'Connected to: localhost') {
    echo "Closure with container access works\n";
}

echo "Done\n";

?>
--EXPECT--
Closure binding works
Closure with parameters works
Alias works
has() works with alias
Multiple aliases work
bound() works with alias
Singleton closure works
Closure returning scalar works
Closure with container access works
Done
