--TEST--
Container: Tagging
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

class EmailNotifier
{
    public $type = 'email';
}

class SmsNotifier
{
    public $type = 'sms';
}

class PushNotifier
{
    public $type = 'push';
}

// Bind notifiers
Container::bind(EmailNotifier::class);
Container::bind(SmsNotifier::class);
Container::bind(PushNotifier::class);

// Tag them
Container::tag([
    EmailNotifier::class,
    SmsNotifier::class,
    PushNotifier::class
], 'notifiers');

// Resolve tagged
$notifiers = Container::tagged('notifiers');

if (count($notifiers) === 3) {
    echo "All tagged services resolved\n";
}

$types = array_map(fn($n) => $n->type, $notifiers);
sort($types);

if ($types === ['email', 'push', 'sms']) {
    echo "Tagged services are correct\n";
}

// Test that tagged() returns actual instances (not null)
$allValid = true;
foreach ($notifiers as $notifier) {
    if ($notifier === null || !is_object($notifier)) {
        $allValid = false;
        break;
    }
}
if ($allValid) {
    echo "Tagged returns valid instances\n";
}

// Test empty tag
$empty = Container::tagged('nonexistent');
if (count($empty) === 0) {
    echo "Empty tag returns empty array\n";
}


// Test tagging singletons
class FileLogger { public $type = 'file'; }
class DatabaseLogger { public $type = 'db'; }

Container::singleton(FileLogger::class);
Container::singleton(DatabaseLogger::class);
Container::tag([FileLogger::class, DatabaseLogger::class], 'loggers');

$loggers1 = Container::tagged('loggers');
$loggers2 = Container::tagged('loggers');

// Singletons should return same instances
if ($loggers1[0] === $loggers2[0] && $loggers1[1] === $loggers2[1]) {
    echo "Tagged singletons work\n";
}

// Test multiple tags on same service
Container::tag([FileLogger::class], 'file-handlers');
Container::tag([FileLogger::class], 'all-handlers');

$fileHandlers = Container::tagged('file-handlers');
$allHandlers = Container::tagged('all-handlers');

if (count($fileHandlers) === 1 && count($allHandlers) === 1) {
    echo "Multiple tags per service work\n";
}

echo "Done\n";

?>
--EXPECT--
All tagged services resolved
Tagged services are correct
Tagged returns valid instances
Empty tag returns empty array
Tagged singletons work
Multiple tags per service work
Done
