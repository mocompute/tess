#module main

// Tagged union with 6 variants
Token: | IntLit { v: Int }
       | FloatLit { v:  Float }
       | BoolLit { v:   Bool }
       | StringLit { v: CString }
       | Ident { name:  CString }
       | Eof

classify(tok: Token) -> Int {
    when tok {
        i: IntLit { 1 }
        f: FloatLit { 2 }
        b: BoolLit { 3 }
        s: StringLit { 4 }
        d: Ident { 5 }
        e: Eof { 0 }
    }
}

main() {
    error := 0

    // Test exhaustive matching on all 6 variants
    error += if classify(IntLit(42)) == 1 {0} else {1}
    error += if classify(FloatLit(3.14)) == 2 {0} else {1}
    error += if classify(BoolLit(true)) == 3 {0} else {1}
    error += if classify(StringLit("hi")) == 4 {0} else {1}
    error += if classify(Ident("foo")) == 5 {0} else {1}
    error += if classify(Eof()) == 0 {0} else {1}

    // Test else arm with many variants
    tok        := Ident("x")
    is_literal := when tok {
        i: IntLit { 1 }
        f: FloatLit { 1 }
        b: BoolLit { 1 }
        s: StringLit { 1 }
        else { 0 }
    }
    error += if is_literal == 0 {0} else {1}

    // Test extracting values from different variants
    tok1 := IntLit(99)
    tok2 := FloatLit(2.5)

    v1 := when tok1 {
        i: IntLit { i.v }
        else { 0 }
    }
    error += if v1 == 99 {0} else {1}

    v2 := when tok2 {
        f: FloatLit { f.v }
        else { 0.0 }
    }
    error += if v2 == 2.5 {0} else {1}

    error
}
