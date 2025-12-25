#module main
#import <stdlib.tl>

_BumpHeader : {
  next:     Ptr(_BumpHeader),
  capacity: CSize,
  size:     CSize,
}

_BumpHeader_create(capacity) {
  c_assert(capacity > sizeof(_BumpHeader))
  out: Ptr(_BumpHeader) := c_malloc(capacity) // FIXME: report an error because this is untyped
  out->next     = null
  out->capacity = capacity
  out->size     = sizeof(_BumpHeader)
  out // FIXME: not enough info to infer type, so it should error
}


main() {
  bh := _BumpHeader_create(16)
  0
}
