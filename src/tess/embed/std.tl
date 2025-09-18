// -- intrinsic function types --

let _tl_sizeof_  : any -> int
let _tl_sizeoft_ : any -> int

let _tl_add_     : (a, a) -> a
let _tl_sub_     : (a, a) -> a
let _tl_mod_     : (a, a) -> a
let _tl_mul_     : (a, a) -> a
let _tl_div_     : (a, a) -> a

let _tl_lt_      : (a, a) -> bool
let _tl_lte_     : (a, a) -> bool
let _tl_eq_      : (a, a) -> bool
let _tl_neq_     : (a, a) -> bool
let _tl_gte_     : (a, a) -> bool
let _tl_gt_      : (a, a) -> bool

let _tl_and_     : (..., ) -> bool
let _tl_or_      : (..., ) -> bool

let _tl_band_    : (a, a) -> a
let _tl_bor_     : (a, a) -> a
let _tl_bxor_    : (a, a) -> a
let _tl_bcomp_   : a -> a

let _tl_bsl_     : (a, int) -> a
let _tl_bsr_     : (a, int) -> a

// -- c stdlib function types --

let c_malloc     : int -> *any
let c_free       : *any -> ()

let std_dbg      : (..., ) -> int

// -- tl lib short-named function implementations --

let sizeof x     : any -> int = _tl_sizeof_ x

let add l r      : (a, a) -> a = _tl_add_ l r
let sub l r      : (a, a) -> a = _tl_sub_ l r
let mod l r      : (a, a) -> a = _tl_mod_ l r
let mul l r      : (a, a) -> a = _tl_mul_ l r
let div l r      : (a, a) -> a = _tl_div_ l r

let lt  l r      : (a, a) -> bool = _tl_lt_  l r
let lte l r      : (a, a) -> bool = _tl_lte_ l r
let eq  l r      : (a, a) -> bool = _tl_eq_  l r
let neq l r      : (a, a) -> bool = _tl_neq_ l r
let gte l r      : (a, a) -> bool = _tl_gte_ l r
let gt  l r      : (a, a) -> bool = _tl_gt_  l r

let band l r     : (a, a) -> a = _tl_band_ l r
let bor  l r     : (a, a) -> a = _tl_bor_  l r
let bxor l r     : (a, a) -> a = _tl_bxor_ l r
let bcomp x      : a -> a      = _tl_bcomp_ x

let bsl arg n    : (a, int) -> a = _tl_bsl_ arg n
let bsr arg n    : (a, int) -> a = _tl_bsr_ arg n

let decr x       : a -> a = sub x 1
let incr x       : a -> a = add x 1
