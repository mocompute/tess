let _tl_sizeof_  : any -> int

let _tl_add_     : (a, a) -> a
let _tl_sub_     : (a, a) -> a
let _tl_mod_     : (a, a) -> a
let _tl_mul_     : (a, a) -> a
let _tl_div_     : (a, a) -> a

let _tl_band_    : (a, a) -> a
let _tl_bor_     : (a, a) -> a
let _tl_bxor_    : (a, a) -> a

let _tl_lt_      : (a, a) -> bool
let _tl_lte_     : (a, a) -> bool
let _tl_eq_      : (a, a) -> bool
let _tl_gte_     : (a, a) -> bool
let _tl_gt_      : (a, a) -> bool

let c_malloc     : int -> *any
let c_free       : *any -> ()

let add lhs rhs  : (a, a) -> a = _tl_add_ lhs rhs
let sub lhs rhs  : (a, a) -> a = _tl_sub_ lhs rhs
let mod lhs rhs  : (a, a) -> a = _tl_mod_ lhs rhs
let mul lhs rhs  : (a, a) -> a = _tl_mul_ lhs rhs
let div lhs rhs  : (a, a) -> a = _tl_div_ lhs rhs

let band lhs rhs : (a, a) -> a = _tl_band_ lhs rhs
let bor  lhs rhs : (a, a) -> a = _tl_bor_  lhs rhs
let bxor lhs rhs : (a, a) -> a = _tl_bxor_ lhs rhs

let lt  lhs rhs  : (a, a) -> bool = _tl_lt_  lhs rhs
let lte lhs rhs  : (a, a) -> bool = _tl_lte_ lhs rhs
let eq  lhs rhs  : (a, a) -> bool = _tl_eq_  lhs rhs
let gte lhs rhs  : (a, a) -> bool = _tl_gte_ lhs rhs
let gt  lhs rhs  : (a, a) -> bool = _tl_gt_  lhs rhs

let incr x       : a -> a = add x 1
