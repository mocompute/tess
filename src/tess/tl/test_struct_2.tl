struct base =
    x : int
end

struct derived =
    base : base
    y : int
end

let main () =
    let v1 = derived (base 1) 2 in
    let () = std_dbg "y = %i\n" v1.y in
    let () = v1.y := 0 in
    let () = std_dbg "y = %i\n" v1.y in
    v1.y
