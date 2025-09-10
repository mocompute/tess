struct point =
       x : int ;
       y : int
end

// recall we want to keep everything prefix or infix, not postfix

let pp : * point = c_malloc 16
let pp : point * = c_malloc 16

let p = point 1 2 in ... 

let tup : (int, int) = (1, 2)
let ptup : *(int, int) = &tup
let ptup : (int, int)* = &tup


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


        !p.x := 1;  
        p!.x := 1;
        
        (deref p) . x := 1 ;
        

        p:x := 1;
        p:x <- 1;

        a : int <- 1;           // oof

        * ptr.x := 1
        obj.x := 1 ;
        obj.y := 2 ;
        0 ;
    end
    
    
let map f xs =
     for 
