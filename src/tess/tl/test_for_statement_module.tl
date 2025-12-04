#module main
#import <Array.tl>

main() {
  xs := Array.with_capacity_undefined(Int, 4)
  for it.& in IndexedArray xs {
    it.ptr.* = it.index + 1
  }

  sum := 0
  for it in IndexedArray xs {
    sum = sum + it.value
  }
  sum - 10
}
