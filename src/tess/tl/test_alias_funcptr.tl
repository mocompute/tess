#module Lib
double(x: Int) -> Int { x * 2 }

#module main
#alias Lib L

main() {
    fp := L.double/1
    result : CInt := fp(21) - 42
}
