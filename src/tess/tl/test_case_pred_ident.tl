#module main
#import <string.tl>

streq(s1, s2) { 0 == c_strcmp(s1, s2) }

main() {
  s1 := "hello"
  case s1, streq {
    "world"    { 1 }
    "hello"    { 0 }
    "there"    { 1 }
  }
}
