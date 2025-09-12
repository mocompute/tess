struct Point =
       x : int ;
       y : int
end

let main ()  =
    let pt = Point 1 2 in
    let (p : * Point) = c_malloc (_tl_sizeof_ pt) in begin
    std_dbg "p = %p\n" p ;
    p.*.x := 12313;             // FIXME parser is greedy an setting x to 0
    p.*.y := 100
    std_dbg "p.*.x = %i\n" p.*.x ;
    std_dbg "p.*.y = %i\n" p.*.y ;
    c_free p ;
    0
    end
