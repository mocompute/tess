// Pre-existing bug: wrapping Ptr[HashMap[K, V]] inside another generic struct
// causes expected_pointer when the outer struct is used with Ptr[T] receiver.

#module Lookup

#import <HashMap.tl>

Lookup[K, V] : { map: Ptr[HashMap[K, V]] }

count[K, V](self: Ptr[Lookup[K, V]]) -> CSize {
    self->map.size()
}

create[K, V]() -> Lookup[K, V] {
    Lookup[K, V](map = HashMap.create[K, V]())
}

destroy[K, V](self: Ptr[Lookup[K, V]]) -> Void {
    self->map.destroy()
}

#module main

main() {
    lk := Lookup.create[Int, Int]()
    error := if lk.count() == 0 {0} else {1}
    lk.destroy()
    error
}
