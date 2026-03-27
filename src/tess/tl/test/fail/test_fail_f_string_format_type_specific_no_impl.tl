// A custom type that implements ToString but NOT ToStringFormat.
// Using type-specific format specifiers (.2f) on it should be a compile error.
#module Point
#import <Print.tl>
#import <ToString.tl>

Point: { x: Int, y: Int }

to_string(p: Point) -> String {
    f"({p.x}, {p.y})"
}

#module main

main() {
    p := Point(x = 1, y = 2)
    s := f"{p:.2f}"
    0
}
