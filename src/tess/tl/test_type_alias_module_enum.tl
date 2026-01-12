#import "builtin.tl"

#module Foo
Status : {Ok, Error, Pending}

#module Bar
Result = Foo.Status

#module main
main() {
    r : Bar.Result := Foo.Status.Ok
    0
}
