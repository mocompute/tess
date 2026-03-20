// Slice helpers and iterator interface.
// The Slice[T] type itself is defined in builtin.tl (auto-available everywhere).
// Import this module explicitly when iteration or helpers are needed.

#module Slice

get[T](s: Slice[T], i: CSize) -> T {
    s.v.[i]
}

is_empty[T](s: Slice[T]) -> Bool {
    s.size == 0
}

// -- Iterator interface --

Iter[T]: {
    v:     Ptr[T],
    size:  CSize,
    index: CSize,
}

iter_init[T]  (s: Ptr[Slice[T]]) -> Iter[T]
iter_ptr[T]   (iter: Ptr[Iter[T]]) -> Ptr[T]
iter_value[T] (iter: Ptr[Iter[T]]) -> T
iter_cond[T]  (iter: Ptr[Iter[T]]) -> Bool
iter_update[T](iter: Ptr[Iter[T]]) -> Void
iter_deinit[T](iter: Ptr[Iter[T]]) -> Void

iter_init  (s)    { Iter(v = s->v, size = s->size, index = 0) }
iter_ptr   (iter) { iter->v.[iter->index].&                    }
iter_value (iter) { iter->v.[iter->index]                      }
iter_cond  (iter) { iter->index < iter->size                   }
iter_update(iter) { iter->index = iter->index + 1              }
iter_deinit(iter) {                                            }
