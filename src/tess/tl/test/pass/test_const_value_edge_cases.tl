#module main
#import <cstdio.tl>

main() {
    // Tagged union destructuring: when-bindings are mutable
    x: Const[Option[Int]] := Some(42)
    result := when x {
        s: Some {
            val := s.v
            val = val + 1
            val
        }
        else { 0 }
    }
    c_printf(c"result = %lld\n", result)

    0
}
