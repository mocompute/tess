// Adversarial stress test: generic tagged unions with type aliases.
// Tests cache key normalization, nested generics, cross-module usage,
// and auto-collapse interaction.

// --- Module A: define a generic tagged union ---
#module A

Res[T]: | ROk { v: T }
        | RErr { code: Int }

// --- Module B: multiple aliases to the same specialization ---
#module B

IntRes1 = A.Res[Int]
IntRes2 = A.Res[Int]

// --- Module C: chained alias ---
#module C

IR = B.IntRes1

// --- Module D: two-param generic tagged union ---
#module D

Either[L, R]: | Left { v: L }
              | Right { v: R }

IntStrEither = Either[Int, Int]

// --- Module E: auto-collapse interaction ---
// Module name matches its tagged union type via auto-collapse
#module Wrapper

T[X]: | Wrap { inner: X }
      | WrapEmpty

// --- Module F: alias of auto-collapsed type ---
#module F

IntWrap = Wrapper[Int]

#module main

// Generic function to extract from A.Res
unwrap_res[T](r: A.Res[T], default: T) -> T {
    when r {
        o: ROk { o.v }
        e: RErr { default }
    }
}

// Generic function to extract from D.Either
unwrap_either[L, R](e: D.Either[L, R], left_default: L, right_default: R) -> Int {
    when e {
        l: Left { 1 }
        r: Right { 2 }
    }
}

main() {
    error := 0

    // --- Test 1: Multiple aliases to the same specialization ---
    // IntRes1 and IntRes2 both alias A.Res[Int] -- must share one C struct
    v1: B.IntRes1  := A.ROk(10)
    v2: B.IntRes2  := A.ROk(20)
    v3: A.Res[Int] := A.ROk(30)

    error += if unwrap_res(v1, 0) == 10 { 0 } else { 1 }
    error += if unwrap_res(v2, 0) == 20 { 0 } else { 1 }
    error += if unwrap_res(v3, 0) == 30 { 0 } else { 1 }

    // --- Test 2: Chained alias (alias of alias) ---
    v4: C.IR := A.ROk(40)
    error += if unwrap_res(v4, 0) == 40 { 0 } else { 1 }

    // --- Test 3: Two-param generic tagged union with alias ---
    e1: D.IntStrEither     := D.Left(v = 100)
    e2: D.Either[Int, Int] := D.Right(v = 200)

    r1 := when e1 {
        l: Left { l.v }
        r: Right { r.v }
    }
    error += if r1 == 100 { 0 } else { 1 }

    r2 := when e2 {
        l: Left { l.v }
        r: Right { r.v }
    }
    error += if r2 == 200 { 0 } else { 1 }

    // Both alias and generic form in same generic function call
    error += if unwrap_either(e1, 0, 0) == 1 { 0 } else { 1 }
    error += if unwrap_either(e2, 0, 0) == 2 { 0 } else { 1 }

    // --- Test 4: Cross-module usage (define in A, alias in B, use in main) ---
    cross1: B.IntRes1 := A.RErr(code = 5)
    cross2: B.IntRes2 := A.RErr(code = 6)
    cross3: C.IR      := A.RErr(code = 7)

    m1 := when cross1 { o: ROk { o.v } e: RErr { e.code } }
    m2 := when cross2 { o: ROk { o.v } e: RErr { e.code } }
    m3 := when cross3 { o: ROk { o.v } e: RErr { e.code } }

    error += if m1 == 5 { 0 } else { 1 }
    error += if m2 == 6 { 0 } else { 1 }
    error += if m3 == 7 { 0 } else { 1 }

    // --- Test 5: Auto-collapse + alias interaction ---
    w1               := Wrapper.Wrap(42)
    w2: F.IntWrap    := Wrapper.Wrap(99)
    w3: Wrapper[Int] := Wrapper.WrapEmpty

    r3 := when w1 { w: Wrap { w.inner } e: WrapEmpty { 0 } }
    r4 := when w2 { w: Wrap { w.inner } e: WrapEmpty { 0 } }
    r5 := when w3 { w: Wrap { w.inner } e: WrapEmpty { -1 } }

    error += if r3 == 42 { 0 } else { 1 }
    error += if r4 == 99 { 0 } else { 1 }
    error += if r5 == -1 { 0 } else { 1 }

    error
}
