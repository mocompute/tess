#import <Hash.tl>

#module main
#import <String.tl>
#import <Array.tl>
#import <HashMap.tl>

S(s: Ptr[Const[CChar]]) -> String.String { String.from_cstr(s) }

// Bug: `when...else` on a tagged union falls through — the else branch
// executes AFTER the matched branch when:
//   1. The when is nested 3 levels deep inside other when expressions
//   2. The matched branch calls a generic function (e.g. HashMap.set)
//
// Without the generic call, or with `case` listing all variants, it works.

Kind: | FlagBool
      | FlagCount
      | FlagValue

Spec: { name: String, kind: Kind }

find(specs: Array[Spec], name: String) -> Option[CSize] {
    i: CSize := 0zu
    while i < specs.size, i += 1zu {
        if String.eq(specs.v.[i].name, name) { return Some(i) }
    }
    None
}

main() -> CInt {
    error := 0

    specs := Array.empty[Spec]()
    Array.push(specs.&, Spec(name = S(c"output"), kind = FlagValue))

    values := HashMap.create[String, Int]()
    errors := 0

    when String.index_of_char(S(c"output=bar"), '=') {
        eq: Some {
            key := String.slice(S(c"output=bar"), 0zu, eq.v)
            when find(specs, key) {
                si: Some {
                    spec := specs.v.[si.v]
                    when spec.kind {
                        fv: FlagValue {
                            c_fprintf(c_stderr, c"matched FlagValue\n")
                            values.set(key, 1)
                        }
                        else {
                            c_fprintf(c_stderr, c"first\n")
                            errors += 1

                        }
                    }
                }
                n: None {
                    c_fprintf(c_stderr, c"second\n")
                    errors += 1
                }
            }
        }
        no: None {
            c_fprintf(c_stderr, c"third\n")
            errors += 1
        }
    }

    // errors should be 0 (FlagValue matched), value should be stored
    if errors != 0 { error += 1 }
    if values.get(S(c"output")) == null { error += 1 }

    values.destroy()
    Array.free(specs.&)
    ret: CInt := error
    ret
}
