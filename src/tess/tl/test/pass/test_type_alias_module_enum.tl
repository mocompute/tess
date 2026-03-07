#module Foo
Status: {Ok, Error, Pending}

#module Bar
Outcome = Foo.Status

#module main

main() {
    // Via type annotation: access enum through original type, annotate with alias
    r1: Bar.Outcome := Foo.Status.Ok

    // Direct usage: access enum values through the alias
    r2 := Bar.Outcome.Ok

    0
}
