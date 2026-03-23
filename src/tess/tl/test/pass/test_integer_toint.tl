// Test the ToInt trait with variadic functions and direct calls.

#module main
#import <ToInt.tl>
#import <Slice.tl>

sum_while(args: ...ToInt) -> Int {
    total: Int   := 0
    i:     CSize := 0
    while i < args.size, i += 1 {
        total += args.v.[i]
    }
    total
}

sum_for(args: ...ToInt) -> Int {
    total: Int := 0
    for x in Slice args {
        total += x
    }
    total
}

main() {
    // Direct to_int calls
    if (42).to_int() != 42 { c_exit(1) }
    if (true).to_int() != 1 { c_exit(2) }
    if (false).to_int() != 0 { c_exit(3) }

    // Variadic sum via while loop: single int
    if sum_while(10) != 10 { c_exit(4) }

    // Variadic sum via while loop: multiple ints
    if sum_while(1, 2, 3, 4, 5) != 15 { c_exit(5) }

    // Variadic sum via while loop: mixed types (int + bool)
    if sum_while(10, true, false, 20) != 31 { c_exit(6) }

    // Variadic sum via while loop: zero args
    if sum_while() != 0 { c_exit(7) }

    // Variadic sum via for-in loop
    if sum_for(100, 200, 300) != 600 { c_exit(8) }

    // Variadic for-in: zero args
    if sum_for() != 0 { c_exit(9) }

    // Variadic for-in: mixed types
    if sum_for(5, true, 10) != 16 { c_exit(10) }

    0
}
