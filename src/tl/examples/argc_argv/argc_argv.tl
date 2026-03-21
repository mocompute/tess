// Build with:
// tess exe -o example argc_argv.tl
//
// or run directly with:
// tess run argc_argv.tl -- extra args


#module main
#import <cstdio.tl>

main(argc: CInt, argv: Ptr[CString]) {
    c_printf("argc = %i\n", argc)

    i := 0
    while i < argc, i = i + 1 {
        c_printf("%s\n", argv.[i])
    }

    0
}
