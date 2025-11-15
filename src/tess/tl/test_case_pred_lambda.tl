#module main
#import <string.tl>

main() {
  s1 := "hello"
  case n, (s1, s2) { 0 == c_strcmp(s1, s2) } {
    "world"    { 1 }
    "there"    { 1 }
    "hello"    { 0 }
  }
}
