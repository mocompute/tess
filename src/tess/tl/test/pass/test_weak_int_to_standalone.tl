// Unsuffixed integer literals are family-agnostic and can unify with standalone types.
#module main

take_size   (x: CSize)    -> CSize       { x }
take_ptrdiff(x: CPtrDiff) -> CPtrDiff { x    }

main() {
    error := 0
    error += if take_size(0) == 0zu { 0 } else { 1 }
    error += if take_size(42) == 42zu { 0 } else { 1 }
    error += if take_ptrdiff(42) == 42z { 0 } else { 1 }
    error
}
