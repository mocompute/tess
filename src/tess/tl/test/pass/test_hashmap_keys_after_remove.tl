// Test HashMap.keys() after removals: no tombstone keys leak through.
#module main
#import <HashMap.tl>
#import <Array.tl>

main() {
    error: CInt := 0
    m           := HashMap.create[Int, Int]()

    // Insert 10 entries
    i := 0
    while i < 10, i += 1 {
        HashMap.set(m, i, i * 10)
    }

    // Remove 5 entries (0..4)
    i = 0
    while i < 5, i += 1 {
        HashMap.remove(m, i)
    }

    // keys() should return exactly 5
    ks := HashMap.keys(m)
    if ks.size != 5zu { error += 1 }

    // Verify no removed keys leak through
    j: CSize := 0zu
    while j < ks.size, j += 1zu {
        if ks.v.[j] < 5 { error += 1 }
    }

    // Insert after removal
    HashMap.set(m, 100, 1000)
    ks2 := HashMap.keys(m)
    if ks2.size != 6zu { error += 1 }

    // Verify 100 is in the keys
    found_100 := false
    j = 0zu
    while j < ks2.size, j += 1zu {
        if ks2.v.[j] == 100 { found_100 = true }
    }
    if !found_100 { error += 1 }

    Array.free(ks.&)
    Array.free(ks2.&)
    HashMap.destroy(m)
    error
}
