#module main
#import <Print.tl>

main() {
    x := 42
    s := f"outer {f"inner {x}"}"
    0
}
