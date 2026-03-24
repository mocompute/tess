#module main
#import <cstring.tl>

#ifc
#define TEST_BUF_SIZE 16
#endc

Buf: { _data: CArray[Byte, c_TEST_BUF_SIZE] }

main() {
    fails  := 0
    b: Buf := void
    c_memset(b.&, 0, sizeof[Buf]())
    if sizeof[Buf]() != 16zu {
        c_printf(c"FAIL sizeof(Buf) = %zu, expected 16\n", sizeof[Buf]())
        fails += 1
    }
    if fails == 0 { c_printf(c"All carray_c_macro tests passed.\n") }
    fails
}
