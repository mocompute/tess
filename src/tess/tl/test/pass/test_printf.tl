#module main
#import <stdio.tl>

main(argc: Int, argv: Ptr[CString]) {
    c_printf("argc = %i\n", argc)
    result: CInt := argc - 1
    result
}
