#module main

T[a]: {
    v:        Ptr[a],
    size:     Int,
    capacity: Int,
}

empty[a]() {
    T ( v: Ptr[a] = null, capacity = 0, size = 0 )
}

main() {
    x := empty[Int]()
    result : CInt := x.size
    result
}
