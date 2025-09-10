struct point =
       x : int ;
       y : int
end

let main ()  =
    let (p : ptr point) = c_malloc 16 in
    let obj = deref p in
    begin
        p x := 1 ;
        p.x := 1 ;

        p..x := 1;
        .p.x := 1;


        .p.x := 1;

        .p.x = 1 ;




        p:x := 1;
        p:x <- 1;

        a : int <- 1;           // oof

        * ptr.x := 1
        obj.x := 1 ;
        obj.y := 2 ;
        0 ;
    end
