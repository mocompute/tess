#module M
f(x: CInt)          -> CInt { x     }
f(x: CInt, y: CInt) -> CInt { x + y }

#module M.Sub
g() -> CInt { 1 }

#module M
h(x: CInt) -> CInt { f(x) + f(x, 1) }

#module main
main() { M.h(10) - 21 }
