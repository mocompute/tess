#module main
#import <Str.tl>
#import <Array.tl>
#import <HashMap.tl>

S(s: Ptr[Const[CChar]]) -> Str.Str { Str.from_cstr(s) }

// Bug: `when...else` on a tagged union falls through — the else branch
// executes AFTER the matched branch when:
//   1. The when is nested 3 levels deep inside other when expressions
//   2. The matched branch calls a generic function (e.g. HashMap.set)
//
// Without the generic call, or with `case` listing all variants, it works.

Kind: | FlagBool
      | FlagCount
      | FlagValue

Spec: { name: Str, kind: Kind }

find(specs: Array[Spec], name: Str) -> Option[CSize] {
    i: CSize := 0zu
    while i < specs.size, i += 1zu {
        if Str.eq(specs.v.[i].name, name) { return Some(i) }
    }
    None
}

main() -> CInt {
    error := 0

    specs := Array.empty[Spec]()
    Array.push(specs.&, Spec(name = S("output"), kind = FlagValue))

    values := HashMap.create[Str, Int]()
    errors := 0

    when Str.index_of_char(S("output=bar"), '=') {
        eq: Some {
            key := Str.slice(S("output=bar"), 0zu, eq.v)
            when find(specs, key) {
                si: Some {
                    spec := specs.v.[si.v]
                    when spec.kind {
                        fv: FlagValue { HashMap.set(values, key, 1) }
                        else          { errors += 1 }
                    }
                }
                n: None { errors += 1 }
            }
        }
        no: None { errors += 1 }
    }

    // errors should be 0 (FlagValue matched), value should be stored
    if errors != 0 { error += 1 }
    if HashMap.get(values, S("output")) == null { error += 1 }

    HashMap.destroy(values)
    Array.free(specs.&)
    ret: CInt := error
    ret
}
