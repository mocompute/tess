#module Foo

Pair[a] : { x: a, y: a }

// 'b' is not a type parameter of Shape — should be rejected
Shape[a] : | Circle { radius: a }
           | Foo.Pair[b]

#module main

main() { 0 }
