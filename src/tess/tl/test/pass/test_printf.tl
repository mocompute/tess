#module main
#import <cstdio.tl>

main(argc: Int, argv: Ptr[CString]) {
    c_printf(c"argc = %i\n", argc)
    result: CInt := argc - 1
    result
}
