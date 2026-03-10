#module Lib
double(x: Int) -> Int { x * 2 }

#module main
#alias L Lib

main() {
    fp           := L.double/1
    result: CInt := fp(21) - 42
    result
}
