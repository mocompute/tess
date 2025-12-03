#module main
#import <Array.tl>

main() {

  xs := Array.with_capacity(Int, 4)
  xs.size = 4
  i := 1
  for x.& in xs {
    x.* = i
    i = i + 1
  }

  sum := 0
  for x in xs {
    sum = sum + x
  }
  sum - 10
}
