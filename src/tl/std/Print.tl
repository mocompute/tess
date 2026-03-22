// -- Print functions --
#module Print
#import <ToString.tl>
#import <String.tl>
#import <cstdio.tl>

_print_all(args: Slice[String]) -> Void

_print_all(args) {
    i: CSize := 0
    while i < args.size, i += 1 {
        s := args.v.[i]
        c_printf(c"%s", s.&.cstr())
    }
    void
}

print(args: ...ToString) -> Void { _print_all(args) }

println(args: ...ToString) -> Void {
    _print_all(args)
    c_printf(c"\n")
    void
}
