#module Foo
Status: {Ok, Error, Pending}

#module Bar
Result = Foo.Status

#module main

main() {
    // Direct usage: access enum values through the alias
    r := Bar.Result.Ok
    0
}
