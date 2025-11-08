#module Array
#import <stdlib.tl>

Array(T) : {
  v:    Ptr(T),
  size: Int,
}

new(T : Type, count) {
  p : Ptr(T) := c_malloc(count * sizeof(T))
  Array { v = p, size = count }
}

free(arr) { c_free(arr.v) }
