#module main

// Bug: storing a closure/lambda in a generic struct's fn ptr field
// produces incorrect specializations and missing closure context types.
// Compiler errors:
//   - unknown type name 'tl_ctx_...' (closure context struct not generated)
//   - 'Processor_N' has no member named 'run' (wrong specialization)

Processor[T]: { run: (T) -> T }

// Lambda/closure stored in a generic struct's fn ptr field, specialized at multiple types

make_adder_processor(n: Int) -> Processor[Int] {
    f := (x) { x + n }
    Processor[Int](run = f)
}

make_negator_processor() -> Processor[Bool] {
    f := (x) { !x }
    Processor[Bool](run = f)
}

main() {
    error := 0

    // Closure capturing n=10 in Processor[Int]
    p1 := make_adder_processor(10)
    r1 := p1.run(5)
    error += if r1 == 15 { 0 } else { 1 }

    // Different capture
    p2 := make_adder_processor(100)
    r2 := p2.run(7)
    error += if r2 == 107 { 0 } else { 1 }

    // Closure in Processor[Bool]
    p3 := make_negator_processor()
    r3 := p3.run(true)
    error += if r3 == false { 0 } else { 1 }

    error
}
