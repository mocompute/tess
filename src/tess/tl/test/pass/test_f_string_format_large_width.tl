#module main
#import <Print.tl>

main() {
    error := 0

    // Width > 256 exercises the heap-allocation path in apply_layout
    s := f"{'x':>300}"
    if s.len() != 300 { error += 1 }

    // Width > 4096 gets capped
    s2 := f"{'y':>5000}"
    if s2.len() != 4096 { error += 1 }

    error
}
