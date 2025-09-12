let _tl_add_ : (a, a) -> a
let _tl_sizeof_ : any -> int
let _tl_sub_ : (a, a) -> a

let c_malloc : int -> *any
let c_free : *any -> ()

let add lhs rhs : (a, a) -> a = _tl_add_ lhs rhs
let sub lhs rhs : (a, a) -> a = _tl_sub_ lhs rhs

let incr x : (a,) -> a = add x 1
