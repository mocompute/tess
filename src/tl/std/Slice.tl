// Slice helpers and iterator interface.
// The Slice[T] type itself is defined in builtin.tl (auto-available everywhere).
// Import this module explicitly when iteration or helpers are needed.

#module Slice

(s: Ptr[Const[Slice[T]]]): {
    get(i: CSize) -> T {
        if i >= a->size { _tl_fatal_(c"Slice.get: index out of bounds") }
        s->data.[i]
    }

    is_empty() -> Bool {
        s->size == 0
    }
}

// -- Iterator interface --

Iter[T]: {
    data:  Ptr[T],
    size:  CSize,
    index: CSize,
}

iter_init[T](s: Ptr[Slice[T]]) { Iter(data = s->data, size = s->size, index = 0) }

(iter: Ptr[Iter[T]]): {
    iter_ptr   () -> Ptr[T] { iter->data.[iter->index].& }
    iter_value () -> T      { iter->data.[iter->index]   }
    iter_cond  () -> Bool   { iter->index < iter->size   }
    iter_update() -> Void   { iter->index += 1           }
    iter_deinit() -> Void   {                            }
}
