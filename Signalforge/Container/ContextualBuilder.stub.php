<?php
/**
 * Signalforge Container Extension
 * ContextualBuilder.stub.php - IDE stub for ContextualBuilder class
 *
 * @package Signalforge\Container
 */

namespace Signalforge\Container;

/**
 * Builder for defining contextual bindings.
 *
 * @final
 */
final class ContextualBuilder
{
    /**
     * Define the abstract type that needs to be resolved.
     *
     * @param string $abstract The abstract type
     * @return self
     */
    public function needs(string $abstract): self {}

    /**
     * Define the concrete implementation to use.
     *
     * @param mixed $implementation The concrete implementation (class name or closure)
     * @return void
     */
    public function give(mixed $implementation): void {}
}

