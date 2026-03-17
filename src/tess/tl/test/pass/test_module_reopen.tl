#module Foo
FooData:                        { x: CInt        }
create_data(x: CInt) -> FooData { FooData(x = x) }

#module Foo.Bar
BarData:                     { y: CInt        }
make_bar(y: CInt) -> BarData { BarData(y = y) }

#module Foo

create_pair(x: CInt, y: CInt) -> FooData {
    bar := Foo.Bar.make_bar(y)
    create_data(x + bar.y)
}

#module main

main() {
    result := Foo.create_pair(10, 32)
    result.x - 42
}
