#module main
#import <cstring.tl>

streq(a, b) { 0 == c_strcmp(a, b) }

main() {
    s1 := c"hello"
    case s1, streq/2 {
        c"world" { 1 }
        c"hello" { 0 }
        c"there" { 1 }
    }
}
