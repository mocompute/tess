let ex1 () =
    let tup = (a = 1, b = 0)
    let ptr = &tup
    let deref = ptr.*
    deref.b

let ex2 () =
    let tup = (a = 1, b = 0)
    let ptr = &tup
    in ptr.*.b

let ex3 () =
    let ptr = &(a = 1, b = 0)
    ptr.*.b

let main () =
    let err = 0
    let err = err + ex1 ()
    let err = err + ex2 ()
    let err = err + ex3 ()
    in err
