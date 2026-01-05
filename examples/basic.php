<?php
/**
 * Basic usage example for Signalforge Container
 * 
 * This example demonstrates the features of the container extension.
 * The extension must be loaded (no stub files needed).
 */

use Signalforge\Container\Container;

// ============================================================================
// Example 1: Basic Autowiring
// ============================================================================

class Logger
{
    public function log($message)
    {
        echo "[LOG] $message\n";
    }
}

class Database
{
    public function connect()
    {
        echo "[DB] Connected\n";
    }
}

class UserRepository
{
    private $database;
    private $logger;
    
    public function __construct(Database $database, Logger $logger)
    {
        $this->database = $database;
        $this->logger = $logger;
    }
    
    public function findUser($id)
    {
        $this->logger->log("Finding user $id");
        $this->database->connect();
        return "User $id";
    }
}

echo "=== Example 1: Basic Autowiring ===\n";
$repo = Container::make(UserRepository::class);
$repo->findUser(42);
echo "\n";

// ============================================================================
// Example 2: Interfaces and Bindings
// ============================================================================

interface CacheInterface
{
    public function get($key);
    public function set($key, $value);
}

class RedisCache implements CacheInterface
{
    public function get($key) { return "redis:$key"; }
    public function set($key, $value) { echo "Redis set: $key = $value\n"; }
}

class FileCache implements CacheInterface
{
    public function get($key) { return "file:$key"; }
    public function set($key, $value) { echo "File set: $key = $value\n"; }
}

echo "=== Example 2: Interface Binding ===\n";

// Bind interface to implementation
Container::bind(CacheInterface::class, RedisCache::class);

class CacheService
{
    private $cache;
    
    public function __construct(CacheInterface $cache)
    {
        $this->cache = $cache;
    }
    
    public function getValue($key)
    {
        return $this->cache->get($key);
    }
}

$service = Container::make(CacheService::class);
echo $service->getValue('user:1') . "\n\n";

// ============================================================================
// Example 3: Singletons
// ============================================================================

echo "=== Example 3: Singletons ===\n";

class Config
{
    private static $instances = 0;
    public $id;
    
    public function __construct()
    {
        $this->id = ++self::$instances;
    }
}

// Bind as singleton
Container::singleton(Config::class);

$config1 = Container::make(Config::class);
$config2 = Container::make(Config::class);

echo "Config 1 ID: {$config1->id}\n";
echo "Config 2 ID: {$config2->id}\n";
echo "Same instance: " . ($config1 === $config2 ? 'Yes' : 'No') . "\n\n";

// ============================================================================
// Example 4: Closures
// ============================================================================

echo "=== Example 4: Closure Binding ===\n";

Container::bind('mailer', function($container, $params) {
    $type = $params['type'] ?? 'smtp';
    return "Mailer configured with: $type";
});

$mailer1 = Container::make('mailer');
$mailer2 = Container::make('mailer', ['type' => 'sendmail']);

echo "$mailer1\n";
echo "$mailer2\n\n";

// ============================================================================
// Example 5: Contextual Bindings
// ============================================================================

echo "=== Example 5: Contextual Bindings ===\n";

interface NotificationInterface
{
    public function send($message);
}

class EmailNotification implements NotificationInterface
{
    public function send($message) { echo "Email: $message\n"; }
}

class SmsNotification implements NotificationInterface
{
    public function send($message) { echo "SMS: $message\n"; }
}

class UserNotifier
{
    private $notification;
    
    public function __construct(NotificationInterface $notification)
    {
        $this->notification = $notification;
    }
    
    public function notify($message)
    {
        $this->notification->send($message);
    }
}

class AdminNotifier
{
    private $notification;
    
    public function __construct(NotificationInterface $notification)
    {
        $this->notification = $notification;
    }
    
    public function notify($message)
    {
        $this->notification->send($message);
    }
}

// Different implementations for different contexts
Container::when(UserNotifier::class)
    ->needs(NotificationInterface::class)
    ->give(EmailNotification::class);

Container::when(AdminNotifier::class)
    ->needs(NotificationInterface::class)
    ->give(SmsNotification::class);

$userNotifier = Container::make(UserNotifier::class);
$adminNotifier = Container::make(AdminNotifier::class);

$userNotifier->notify("Welcome!");
$adminNotifier->notify("System alert!");
echo "\n";

// ============================================================================
// Example 6: Tagging
// ============================================================================

echo "=== Example 6: Tagging ===\n";

class Task1 { public function run() { echo "Task 1 running\n"; } }
class Task2 { public function run() { echo "Task 2 running\n"; } }
class Task3 { public function run() { echo "Task 3 running\n"; } }

Container::bind(Task1::class);
Container::bind(Task2::class);
Container::bind(Task3::class);

Container::tag([Task1::class, Task2::class, Task3::class], 'tasks');

$tasks = Container::tagged('tasks');
foreach ($tasks as $task) {
    $task->run();
}
echo "\n";

// ============================================================================
// Example 7: Aliases
// ============================================================================

echo "=== Example 7: Aliases ===\n";

Container::alias(Logger::class, 'log');
Container::alias(Database::class, 'db');

$logger = Container::make('log');
$logger->log("Using alias");
echo "\n";

echo "Done!\n";

