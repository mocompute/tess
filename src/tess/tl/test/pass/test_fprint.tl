// Test print/println after _fprint_all refactor.
// fprint/fprintln are covered by test_variadic_mixed_param_alias.

#module main
#import <Print.tl>
#import <cstdio.tl>

print   = Print.print
println = Print.println

main(argc: Int, argv: Ptr[CString]) -> CInt {
    print("hello")
    print(" ", "world")
    println("!")
    println(42, " is ", true)
    0
}
