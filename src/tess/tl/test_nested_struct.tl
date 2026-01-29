#module main

Foo(T1, T2): {
    Bar: {
        barf: T1
    }

    Baz: {
        bazf: T2
    }

    a_bar: Bar,
    a_baz: Baz,
    a_t2:  T2,
}

main() {
    error := 0
    bar   := Foo.Bar(barf = 42)
    baz   := Foo.Baz(bazf = 100)
    foo   := Foo(a_bar = bar, a_baz = baz, a_t2 = 7)
    error += if foo.a_bar.barf == 42 {0} else {1}
    error += if foo.a_baz.bazf == 100 {0} else {1}
    error += if foo.a_t2 == 7 {0} else {1}
    error
}
