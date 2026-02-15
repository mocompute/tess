#module main

// Tagged unions should be usable as field types even when defined later
// in the same module (forward reference). Currently fails with expected_type.

Outer: | OA { inner: Inner }
       | OB

Inner: | IA { value: Int }
       | IB

main() {
    x := OA(IA(42))

    result := when x {
        oa: OA {
            when oa.inner {
                ia: IA { ia.value }
                ib: IB { 0 }
            }
        }
        ob: OB { 0 }
    }
    result - 42
}
