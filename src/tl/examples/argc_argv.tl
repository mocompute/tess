// From root directory, build with:
// ./tess exe -o example src/tl/examples/argc_argv.tl

#module main
#import <stdio.tl>

main(argc: Int, argv: Ptr[CString]) {
    c_printf(c"argc = %i\n", argc)

    i := 0
    while i < argc, i = i + 1 {
        c_printf(c"%s\n", argv.[i])
    }

    0
}
