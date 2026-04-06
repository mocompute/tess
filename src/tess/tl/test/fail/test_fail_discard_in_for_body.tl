#module main
#import <Array.tl>

make_option() -> Option[Int] {
    Some(1)
}

main() {
    xs := Array.init(0, 3zu)
    for x in xs {
        make_option()                   // discarded in for body — must fail
    }
    0
}
