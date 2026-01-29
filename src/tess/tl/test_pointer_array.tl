#module main

main() {
    // we automatically decay array to pointer, unlike C
    p := CArray(Int, 16)
    p[0] = 123456

    // this gives a Int**, unlike C
    pp := p.&

    // double dereference
    val := pp.*.*
    val - 123456
}
