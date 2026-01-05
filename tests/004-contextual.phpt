--TEST--
Container: Contextual bindings
--EXTENSIONS--
signalforge_container
--FILE--
<?php

use Signalforge\Container\Container;

interface LoggerInterface {}
class FileLogger implements LoggerInterface { public $type = 'file'; }
class DatabaseLogger implements LoggerInterface { public $type = 'database'; }

class UserController {
    public function __construct(public LoggerInterface $logger) {}
}

class AdminController {
    public function __construct(public LoggerInterface $logger) {}
}

// Contextual binding: UserController gets FileLogger
Container::when(UserController::class)
    ->needs(LoggerInterface::class)
    ->give(FileLogger::class);

// Contextual binding: AdminController gets DatabaseLogger
Container::when(AdminController::class)
    ->needs(LoggerInterface::class)
    ->give(DatabaseLogger::class);

$userCtrl = Container::make(UserController::class);
$adminCtrl = Container::make(AdminController::class);

if ($userCtrl->logger instanceof FileLogger) {
    echo "UserController gets FileLogger\n";
}

if ($adminCtrl->logger instanceof DatabaseLogger) {
    echo "AdminController gets DatabaseLogger\n";
}

if ($userCtrl->logger->type === 'file') {
    echo "FileLogger works\n";
}

if ($adminCtrl->logger->type === 'database') {
    echo "DatabaseLogger works\n";
}

echo "Done\n";

?>
--EXPECTF--
UserController gets FileLogger
AdminController gets DatabaseLogger
FileLogger works
DatabaseLogger works
Done

