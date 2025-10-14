// This fails because _tl_first_ is not defined, and therefore there are no named parameters to attach the
// type of its argument to.
// let main () = _tl_first_ (0, 1, 2)

// By contrast, going through a function call makes it work:

let id x = x
let main () = _tl_first_ (id (0, 1, 2))
