<?php
/**
 * Signalforge Container Extension
 * Exceptions - IDE stubs for exception classes
 *
 * @package Signalforge\Container
 */

namespace Signalforge\Container;

use Exception;

/**
 * Base exception for container errors.
 */
class ContainerException extends Exception {}

/**
 * Exception thrown when a type cannot be resolved.
 */
class NotFoundException extends ContainerException {}

/**
 * Exception thrown when a circular dependency is detected.
 */
class CircularDependencyException extends ContainerException {}

