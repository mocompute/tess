#module main
#import <Array.tl>

main() {
  xs := Array.with_capacity_undefined(Int, 4)
  for it.& in IndexedArray xs {
    it.value.* = it.index + 1
  }

  sum := 0
  for x in xs {
    sum = sum + x
  }
  sum - 10
}
