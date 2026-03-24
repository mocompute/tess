// Test function alias for variadic function with mixed fixed + variadic params.

#module main
#import <Print.tl>
#import <cstdio.tl>

fprint   = Print.fprint
fprintln = Print.fprintln

main(argc: Int, argv: Ptr[CString]) -> CInt {
    fprint(c_stdout, "hello ")
    fprintln(c_stdout, "world")
    fprintln(c_stderr, 42, " is ", true)
    0
}
