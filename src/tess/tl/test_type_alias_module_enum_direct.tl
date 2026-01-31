#module Foo
Status: {Ok, Error, Pending}

#module Bar
Outcome = Foo.Status

#module main

main() {
    // Direct usage: access enum values through the alias
    r := Bar.Outcome.Ok
    0
}
