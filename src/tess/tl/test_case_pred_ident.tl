#module main
#import <string.tl>

streq(s1, s2) { 0 == c_strcmp(s1, s2) }

main() {
  s1 := "hello"
  case n, streq {
    "world"    { 1 }
    "there"    { 1 }
    "hello"    { 0 }
  }
}
