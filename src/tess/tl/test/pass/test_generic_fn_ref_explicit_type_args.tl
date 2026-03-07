#module main

// Taking a reference to a generic function with explicit type arguments
// should work: _default_hash[Int]/1

_default_hash[K](key: Ptr[K]) -> UInt { 42u }

main() {
    fn          := _default_hash[Int]/1
    p: Ptr[Int] := null
    r: CInt     := fn(p)
    r - 42
}
