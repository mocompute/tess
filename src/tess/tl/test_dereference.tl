let main () =
    let tup = (a = 1, b = 0)
    let ptr = &tup
    let deref = ptr.*
    deref.b
