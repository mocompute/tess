struct point =
       x : int
       y : int
end

let main ()  =
    let (p : point *) = c_malloc 16 in
    let obj = *p in
    begin
        obj.x := 1;
        obj.y := 2;
        0
    end
