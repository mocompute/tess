struct base =
    x : int
end

struct derived =
    base : base
       y : int
end

let main () =
    let v1 = derived (base 1) 2
    let v2 = v1.base
    let () = std_dbg "x = %i\n" v2.x
    let () = std_dbg "y = %i\n" v1.y
    let () = v1.y := 0
    let () = std_dbg "y = %i\n" v1.y
    in
    v1.y
