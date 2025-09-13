let ex1 () =
    let tup = (a = 1, b = 0) in
    let ptr = &tup in
    let deref = ptr.* in
    deref.b

let ex2 () =
    let tup = (a = 1, b = 0) in
    let ptr = &tup in
    ptr.*.b

let ex3 () =
    let ptr = &(a = 1, b = 0) in
    ptr.*.b

let ex4 () =
    let val = 1 in
    let ptr = &val in
    let () =
        begin ptr.* := 0 end
    in val



let main () =
    let err = 0 in
    let err = add err (ex1 ()) in
    let err = add err (ex2 ()) in
    let err = add err (ex3 ()) in
    let err = add err (ex4 ()) in
    err
