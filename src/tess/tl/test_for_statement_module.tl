#module main
#import <Array.tl>

main() {
  xs := Array.with_capacity_undefined(Int, 4)

  // Note: the for statement refers to the IndexedArray module defined in <Array.tl>.
  // Iterator modules may define arbitrary fields on the iterator, such as `index` in this example.
  // By using `it.&` the caller signals its desire to get a mutable iterator.
  // See the section `Iterator interface` in <Array-tutorial.tl> for more documentation.
  for it.& in IndexedArray xs {
    it.ptr.* = it.index + 1
  }

  sum := 0
  for it in IndexedArray xs {
    sum = sum + it.value
  }
  sum - 10
}
