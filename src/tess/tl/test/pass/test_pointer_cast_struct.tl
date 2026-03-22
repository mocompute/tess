#module main
#import <cstdio.tl>

Foo: { x: Int   }
Bar: { x: Float }

main() {
    floats: CArray[Bar, 8] := void
    floats.[0].x = 0.5

    floats_ptr: Ptr[Bar] := floats
    ints:       Ptr[Foo] := floats_ptr
    c_printf(c"val = %lli\n", ints.[0].x)
    0
}
