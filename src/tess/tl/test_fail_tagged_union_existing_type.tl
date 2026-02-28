#module Foo

Special: { x: Float, y: Float }

// Existing-type variant syntax (Module.Type) is no longer supported
Shape: | Circle { radius: Float }
       | Foo.Special
       | MyNone

#module main

main() { 0 }
