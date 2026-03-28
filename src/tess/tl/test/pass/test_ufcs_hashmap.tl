#import <Hash.tl>

#module main
#import <HashMap.tl>
#import <Array.tl>

// Test: HashMap via implicit module UFCS
// HashMap.create() returns Ptr[HashMap], so receiver is already a pointer.
// This tests the c"no double-wrap" path — no implicit & needed.
main() {
    error := 0

    m := HashMap.create[Int, Int]()

    // Nullary on Ptr — size, is_empty
    error += if m.size() == 0zu {0} else {1}
    error += if m.is_empty() == true {0} else {1}

    // set, contains, get
    m.set(1, 100)
    m.set(2, 200)
    m.set(3, 300)
    error += if m.size() == 3zu {0} else {1}
    error += if m.is_empty() == false {0} else {1}

    error += if m.contains(1) == true {0} else {1}
    error += if m.contains(99) == false {0} else {1}

    // get returns Ptr[V]
    p := m.get(2)
    error += if p.* == 200 {0} else {1}

    // get_copy returns Option[V]
    opt := m.get_copy(3)
    val := when opt {
        s: Some { s.value }
        n: None { -1 }
    }
    error += if val == 300 {0} else {1}

    none_opt := m.get_copy(99)
    none_val := when none_opt {
        s: Some { s.value }
        n: None { -1 }
    }
    error += if none_val == -1 {0} else {1}

    // remove
    error += if m.remove(2) == true {0} else {1}
    error += if m.size() == 2zu {0} else {1}
    error += if m.contains(2) == false {0} else {1}

    // keys — returns Array[K], then chain cross-module
    ks := m.keys()
    error += if ks.size == 2zu {0} else {1}
    ks.free()

    // clear
    m.clear()
    error += if m.size() == 0zu {0} else {1}

    m.destroy()

    error
}
