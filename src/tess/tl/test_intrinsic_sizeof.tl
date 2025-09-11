struct point = x : int; y : int end

let main () =
    let x = point 1 2 in
    begin
        std_dbg "sizeof an point struct is: %u\n" (_tl_sizeof_ x);
        0
    end