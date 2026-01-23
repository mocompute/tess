#module main
#import <string.tl>

streq(a, b) { 0 == c_strcmp(a, b) }

main() {
  s1 := "hello"
  case s1, streq/2 {
    "world"    { 1 }
    "hello"    { 0 }
    "there"    { 1 }
  }
}
