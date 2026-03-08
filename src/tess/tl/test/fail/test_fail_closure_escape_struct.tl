#module main

// Storing a capturing closure in a struct field is rejected because
// the closure context lives on the stack and would dangle.

Processor[T]: { run: (T) -> T }

make_adder_processor(n: Int) -> Processor[Int] {
    f := (x) { x + n }
    Processor[Int](run = f)
}

main() {
    p := make_adder_processor(10)
    r := p.run(5)
    if r == 15 { 0 } else { 1 }
}
