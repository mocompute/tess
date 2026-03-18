#import <Hash.tl>

#module main
#import <HashMap.tl>

// Test: bare HashMap[K, V] in function return type
make_map() -> Ptr[HashMap[Int, Int]] {
    m := HashMap.create[Int, Int]()
    HashMap.set(m, 1, 100)
    m
}

// Test: bare HashMap[K, V] in function parameter type
map_size(m: Ptr[HashMap[Int, Int]]) -> CSize {
    HashMap.size(m)
}

// Test: explicit HashMap.HashMap[K, V] still works
map_size_qualified(m: Ptr[HashMap.HashMap[Int, Int]]) -> CSize {
    HashMap.size(m)
}

// Test: bare and qualified are interchangeable
convert_map(m: Ptr[HashMap[Int, Int]]) -> Ptr[HashMap.HashMap[Int, Int]] { m }

main() -> CInt {
    error: CInt := 0
    m           := make_map()
    if map_size(m) != 1zu { error += 1 }
    if map_size_qualified(m) != 1zu { error += 1 }
    if map_size(convert_map(m)) != 1zu { error += 1 }
    p := HashMap.get(m, 1)
    if p == null { error += 1 }
    else if p.* != 100 { error += 1 }
    HashMap.destroy(m)
    error
}
