struct point =
       x : int ;
       y : int
end

// recall we want to keep everything prefix or infix, not postfix

let pp : * point = c_malloc 16

// what about

let pp : *.point = c_malloc (_tl_sizeof_ point) // this sizeof doesn't work yet

*.(int, int) or *(int, int)
*(a -> b) vs ( *a -> b )

// chaining blocks, only in some cases
let x1 = val 
// in begin are implied
step1; step2; result end 

let x1 = val in
let x2 = step1 x1 in
let x3 = step2 x2 in
let x4 = step3 x3 in
  x4
end

let x1 = val in
  x1 |> step1 |> step2 |> step3
end
// or with keyword "it"
pipe x1 = val
  in  step1 it 
  and step2 it extra 
  and step3 flipped it more
  and it became a function
end
// again, prefer prefix notation for everything 


  


let p = point 1 2 in ...

let tup : (int, int) = (1, 2)
let ptup : *(int, int) = &tup


let main ()  =
    let (p : ptr point) = c_malloc 16 in
    let obj = deref p in
    begin
        p x := 1 ;
        p.x := 1 ;

        p..x := 1;
        .p.x := 1;


        .p.x := 1;              // no good because of chaining .p.x.y.z what is going on?
        .p.x = 1 ;


        !p.x := 1;
        p!.x := 1;

        (deref p) . x := 1 ;


        p:x := 1;
        p:x <- 1;

        a : int <- 1;           // oof

        * ptr . x := 1
        // chaining?
        * ptr . * pp ...
        // or
        ptr . * . pp . * . x ?
        ptr.*.pp.*.x
        ptr.*.y := 2
        (assign (dot (deref ptr) x) 2)


        obj.x := 1 ;
        obj.y := 2 ;
        0 ;
    end


let map f xs =
     for

// compiler intrinsics

let x = _tl_sizeof_ foo
    => (intrinsic-application _tl_sizeof_ (symbol foo))
