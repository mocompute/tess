#module main
#import <cstdio.tl>

// Const[Result] with exhaustive when (no else).

main() {
    x: Const[Result[Int, Int]] := Ok(42)
    result := when x {
        o: Ok { o.v }
        e: Err { e.v }
    }
    c_printf(c"result = %lld\n", result)
    0
}
