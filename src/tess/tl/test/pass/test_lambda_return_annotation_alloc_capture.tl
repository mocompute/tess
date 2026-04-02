#module main
#import <Alloc.tl>

main() {
    error := 0

    // alloc + capture with return type annotation
    base     := 10
    add_base := [[alloc, capture(base)]] (x: Int) -> Int { x + base }
    if add_base(5) != 15 { error += 1 }

    // alloc + capture with return type annotation, no param annotations
    multiplier := 3
    mul        := [[alloc, capture(multiplier)]] (x) -> Int { x * multiplier }
    if mul(4) != 12 { error += 1 }

    // alloc + capture with void return type (uses pointer for mutation visibility)
    acc        := 0
    acc_p      := acc.&
    add_to_acc := [[alloc, capture(acc_p)]] (n: Int) -> Void { acc_p.* += n }
    add_to_acc(7)
    if acc != 7 { error += 1 }

    error
}
