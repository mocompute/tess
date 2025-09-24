let fact x =
    // confirmed that this will be tail call optimised by clang at -O,
    if   lte x 1
    then 1
    else mul x (fact (decr x))

let main () =
    if   eq (fact 20) 2432902008176640000
    then 0
    else 1



// $ ./tess c test_factorial.tl | cc -O -x c - -S -masm=intel -o -
//
// tl_fun_fact_14:                         # @tl_fun_fact_14
//      .cfi_startproc
// # %bb.0:
//      mov	eax, 1
//      cmp	rdi, 2
//      jl	.LBB1_3
//      .p2align	4
// .LBB1_2:                                # =>This Inner Loop Header: Depth=1
//      imul	rax, rdi
//      dec	rdi
//      cmp	rdi, 2
//      jge	.LBB1_2
// .LBB1_3:
//      ret
