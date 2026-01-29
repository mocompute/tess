#module main
#import <stdio.tl>

Foo: { x: Int   }
Bar: { x: Float }

main() {
    floats := CArray(Bar, 8)
    floats[0].x = 0.5

    ints: Ptr(Foo) := floats
    c_printf("val = %lli\n", ints[0].x)
    0
}
