#module Foo
Status: {Ok, Error, Pending}

#module Bar
Outcome = Foo.Status

#module main

main() {
    r: Bar.Outcome := Foo.Status.Ok
    0
}
