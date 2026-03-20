// Stress test: generic tagged unions with type aliases.
// Exercises alias normalization before specialization cache lookups.

#module Defs

Res[T]: | ROk  { v: T }
        | RErr { code: Int }

#module Aliases

IntRes = Defs.Res[Int]

#module NestedAlias

IR = Aliases.IntRes

#module main

// Generic function taking a generic tagged union
extract[T](r: Defs.Res[T], default: T) -> T {
    when r {
        o: ROk { o.v }
        e: RErr { default }
    }
}

// Function taking aliased type as parameter
extract_aliased(r: Aliases.IntRes, default: Int) -> Int {
    when r {
        o: ROk { o.v }
        e: RErr { default }
    }
}

main() {
    error := 0

    // --- Test 1: Use alias and full generic form in same program ---
    via_alias:   Aliases.IntRes := Defs.ROk(10)
    via_generic: Defs.Res[Int]  := Defs.ROk(20)

    error += if extract(via_alias, 0) == 10 { 0 } else { 1 }
    error += if extract(via_generic, 0) == 20 { 0 } else { 1 }

    // --- Test 2: Alias used as function parameter type ---
    ok_val := Defs.ROk(42)
    error += if extract_aliased(ok_val, 0) == 42 { 0 } else { 1 }

    err_val: Aliases.IntRes := Defs.RErr(code = 1)
    error += if extract_aliased(err_val, 99) == 99 { 0 } else { 1 }

    // --- Test 3: Nested alias (alias of alias) ---
    nested_ok: NestedAlias.IR := Defs.ROk(77)
    error += if extract(nested_ok, 0) == 77 { 0 } else { 1 }

    // --- Test 4: Generic function called with aliased and non-aliased ---
    a1: Aliases.IntRes := Defs.ROk(100)
    a2: Defs.Res[Int]  := Defs.ROk(200)
    error += if extract(a1, 0) == 100 { 0 } else { 1 }
    error += if extract(a2, 0) == 200 { 0 } else { 1 }

    // --- Test 5: Pattern matching (when) on aliased tagged union ---
    aliased_val: Aliases.IntRes := Defs.ROk(55)
    matched                     := when aliased_val {
        o: ROk { o.v }
        e: RErr { e.code }
    }
    error += if matched == 55 { 0 } else { 1 }

    // Pattern match on nested alias
    nested_val: NestedAlias.IR := Defs.RErr(code = 7)
    matched2                   := when nested_val {
        o: ROk { o.v }
        e: RErr { e.code }
    }
    error += if matched2 == 7 { 0 } else { 1 }

    error
}
