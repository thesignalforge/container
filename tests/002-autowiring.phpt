--TEST--
Container: Autowiring
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

class Logger
{
    public $name = 'logger';
}

class Database
{
    public $connection = 'mysql';
}

class UserService
{
    public $logger;
    public $database;
    
    public function __construct(Logger $logger, Database $database)
    {
        $this->logger = $logger;
        $this->database = $database;
    }
}

// Test autowiring without explicit bindings
$service = Container::make(UserService::class);

if ($service instanceof UserService) {
    echo "Autowiring works\n";
}

if ($service->logger instanceof Logger) {
    echo "Logger dependency resolved\n";
}

if ($service->database instanceof Database) {
    echo "Database dependency resolved\n";
}

// Test with explicit parameter override
$customLogger = new Logger();
$customLogger->name = 'custom';

$service2 = Container::make(UserService::class, ['logger' => $customLogger]);

if ($service2->logger->name === 'custom') {
    echo "Parameter override works\n";
}


// Test default parameter values are used when no type hint
class ConfigService
{
    public function __construct(
        public string $host = 'localhost',
        public int $port = 3306,
        public bool $debug = false
    ) {}
}

$config = Container::make(ConfigService::class);
if ($config->host === 'localhost' && $config->port === 3306 && $config->debug === false) {
    echo "Default parameters work\n";
}

// Test mixed: type-hinted deps + defaults
class MixedService
{
    public function __construct(
        public Logger $logger,
        public string $env = 'production'
    ) {}
}

$mixed = Container::make(MixedService::class);
if ($mixed->logger instanceof Logger && $mixed->env === 'production') {
    echo "Mixed deps and defaults work\n";
}

// Test override default with explicit param
$mixed2 = Container::make(MixedService::class, ['env' => 'testing']);
if ($mixed2->env === 'testing') {
    echo "Override default param works\n";
}

// Test deep autowiring (nested dependencies)
class CacheService { public $type = 'memory'; }
class Repository {
    public function __construct(public Database $db, public CacheService $cache) {}
}
class Controller {
    public function __construct(public Repository $repo, public Logger $logger) {}
}

$controller = Container::make(Controller::class);
if ($controller->repo->db instanceof Database && 
    $controller->repo->cache instanceof CacheService &&
    $controller->logger instanceof Logger) {
    echo "Deep autowiring works\n";
}

echo "Done\n";

?>
--EXPECT--
Autowiring works
Logger dependency resolved
Database dependency resolved
Parameter override works
Default parameters work
Mixed deps and defaults work
Override default param works
Deep autowiring works
Done

