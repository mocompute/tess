// T: Greet bound where T is nested inside Ptr[Const[T]], called with a Const argument.
// t.& is Ptr[Const[Thing]]. collect_quant_bindings strips Const from both the impl
// (Const[T] -> T) and the concrete side (Const[Thing] -> Thing) to correctly bind
// T = Thing and resolve the Greet trait check.
#module Thing

Thing: { x: CInt }

greet(self: Thing) -> CInt { self.x }

#module main

Greet[T]: {
    greet(self: T) -> CInt
}

poke_const[T: Greet](p: Ptr[Const[T]]) -> CInt {
    t := p.*
    t.greet()
}

main() -> CInt {
    t: Const := Thing(x = 42)
    r        := poke_const(t.&)
    if r == 42 { 0 } else { 1 }
}
