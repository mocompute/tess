// Test: auto-collapse works when a module's primary type is named T.
// A module type named T should be usable via the bare module name as a
// constructor and in type positions, just like same-name types (e.g. Array.Array).

#module Vec2

T : { x: Int, y: Int }

add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y) }

#module Vec3

T : { x: Int, y: Int, z: Int }

add(a: T, b: T) -> T { T(x = a.x + b.x, y = a.y + b.y, z = a.z + b.z) }

#module main

// Test: bare Vec2 as constructor (the key auto-collapse use case)
test_constructor() -> CInt {
    error: CInt := 0
    v := Vec2(x = 10, y = 20)
    if v.x != 10 { error += 1 }
    if v.y != 20 { error += 1 }
    error
}

// Test: bare Vec3 as constructor
test_constructor_vec3() -> CInt {
    error: CInt := 0
    v := Vec3(x = 1, y = 2, z = 3)
    if v.x != 1 { error += 1 }
    if v.y != 2 { error += 1 }
    if v.z != 3 { error += 1 }
    error
}

// Test: explicit Vec2.T still works alongside the alias
test_explicit() -> CInt {
    error: CInt := 0
    v := Vec2.T(x = 5, y = 6)
    if v.x != 5 { error += 1 }
    error
}

// Test: bare and explicit are interchangeable
convert(v: Vec2.T) -> Vec2.T { v }

test_interchangeable() -> CInt {
    error: CInt := 0
    v := Vec2(x = 7, y = 8)
    v2 := convert(v)
    if v2.x != 7 { error += 1 }
    if v2.y != 8 { error += 1 }
    error
}

main() -> CInt {
    error: CInt := 0
    error += test_constructor()
    error += test_constructor_vec3()
    error += test_explicit()
    error += test_interchangeable()
    error
}
