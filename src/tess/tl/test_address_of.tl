let print p =
    begin
        std_dbg "%p\n" p;       // currently has indeterminate type so we need to explicitly return something
        0
    end

let main () =
    let tup = (1, 2)
    let ptr = &tup
    print ptr
