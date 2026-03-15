// Test module opened 3 times with cross-opening calls and overloaded functions.
#module M

f(x: CInt) -> CInt { x + 1 }

#module M.Sub
helper() -> CInt { 100 }

#module M
// Second opening: add g, call f from first
g(x: CInt) -> CInt { f(x) + 10 }

// Overloaded: 2-arg version of f
f(x: CInt, y: CInt) -> CInt { x + y }

#module M
// Third opening: call functions from both prior openings
h(x: CInt) -> CInt { f(x) + g(x) + f(x, x) }

#module main
main() {
    // h(5) = f(5) + g(5) + f(5,5) = 6 + 16 + 10 = 32
    M.h(5) - 32
}
