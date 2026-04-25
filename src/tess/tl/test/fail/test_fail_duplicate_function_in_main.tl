// Tess has no overloading on parameter type, so two functions with the same
// name and arity in the same module collide on their arity-mangled name.
// Before this diagnostic, the loser was silently dropped and call sites failed
// with a confusing "conflicting types" error far from the source of the issue.

#module main

Point: { x: Int, y: Int }
Vec3:  { x: Int, y: Int, z: Int }

hash(p: Point) -> CSize { 0zu }
hash(v: Vec3)  -> CSize { 1zu }

main() { 0 }
