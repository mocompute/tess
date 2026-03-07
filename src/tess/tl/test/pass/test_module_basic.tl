#module ModuleOne

foo := 123

f(a, b) { a + b }


#module ModuleTwo

f(a, b) { a - b }


#module main

main() { ModuleOne.f(-1, 1) }
