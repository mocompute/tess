#module main
#import <Alloc.tl>

// Verify that calling an allocated closure does NOT allocate on each call.
// Uses a bump allocator and checks that stats don't change after creation.

make_adder(n: Int, a: Ptr[Alloc.Allocator]) -> (Int) -> Int {
    f := [[alloc(a), capture(n)]] (x) { x + n }
    f
}

main() {
    arena := Alloc.bump_create(Alloc.context.default, 4096u)

    add10 := make_adder(10, arena)

    // Snapshot stats after closure creation
    stats_before := Alloc.bump_get_stats(arena)

    // Call the closure many times
    sum := 0
    i := 0
    while i < 100 {
        sum += add10(i)
        i += 1
    }

    // Snapshot stats after calls
    stats_after := Alloc.bump_get_stats(arena)

    error := 0

    // Verify correct results
    // sum = sum(i + 10, i=0..99) = sum(i, i=0..99) + 1000 = 4950 + 1000 = 5950
    error += if sum == 5950 { 0 } else { 1 }

    // Verify no new allocations occurred during calls
    error += if stats_before.peak_allocated == stats_after.peak_allocated { 0 } else { 1 }

    error
}
