#module Array
#import <Alloc.tl>
#import <stdlib.tl>

Array(T) : {
  v:            Ptr(T),
  size:         Int,
  capacity:     Int,
}

new(T: Type, count: Int) {
  width := Alloc.align(sizeof(T), alignof(T))
  p : Ptr(T) := c_malloc(count * width)
  Array { v = p, capacity = count, size = 0 }
}

free(arr) { c_free(arr.v) }
