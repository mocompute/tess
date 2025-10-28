// let add1 x = incr x

// let foo f y = f y

// let main () =
//     foo add1 -1


add1(x)  { x + 1 }
foo(f, x) { f(x) }

main() { foo(add1, -1) }
