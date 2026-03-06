#module main

// Duplicate type name: trait and struct cannot share the same name
Vec3 : { x: Int, y: Int, z: Int }

Vec3[T] : {
    add(a: T, b: T) -> T
}

main() { 0 }
