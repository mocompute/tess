#module main

main() {
    arr: CArray(Int, 16) := void
    arr[0] = 123456

    // explicit decay to pointer
    p: Ptr(Int) := arr

    // this gives a Int**
    pp := p.&

    // double dereference
    val := pp.*.*
    val - 123456
}
