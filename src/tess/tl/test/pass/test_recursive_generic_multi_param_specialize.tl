#module main

// Multi-parameter recursive generic tagged union, specialized at two different type combinations
KVList[K, V]: | Entry { key: K, val: V, next: Ptr[KVList[K, V]] }
              | End

main() {
    // Specialization 1: KVList[Int, Bool]
    a_end: KVList[Int, Bool] := End()
    a                        := Entry(key = 1, val = true, next = a_end.&)

    // Specialization 2: KVList[Bool, Int]
    b_end: KVList[Bool, Int] := End()
    b                        := Entry(key = true, val = 42, next = b_end.&)

    error := 0

    when a {
        e: Entry { error += if e.key == 1 { 0 } else { 1 } }
        n: End { error += 1 }
    }

    when b {
        e: Entry { error += if e.val == 42 { 0 } else { 1 } }
        n: End { error += 1 }
    }

    error
}
