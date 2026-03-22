#module main

// Generic function that returns a different int depending on the type of x.
// Each call site is specialized, so the predicates resolve at compile time.
type_id(x) {
    if x ::      Int { 1 }
    else if x :: Float { 2 }
    else { 0 }
}

main() {
    error := 0
    error = error + if type_id(42) == 1 { 0 } else { 1 }
    error = error + if type_id(3.14) == 2 { 0 } else { 1 }
    error = error + if type_id(c"hey") == 0 { 0 } else { 1 }
    error
}

// Verify the generated code is easily optimized:

// ./tess c src/tess/tl/test_type_predicate_branch.tl | cc -x c - -O -S -masm=intel -o -
//      .text
//      .intel_syntax noprefix
//      .file	c"-"
//      .globl	main                            # -- Begin function main
//      .p2align	4, 0x90
//      .type	main,@function
// main:                                   # @main
//      .cfi_startproc
// # %bb.0:
//      xor	eax, eax
//      ret
// .Lfunc_end0:
//      .size	main, .Lfunc_end0-main
//      .cfi_endproc
//                                         # -- End function
//      .ident	c"clang version 19.1.7"
//      .section	c".note.GNU-stack",c"",@progbits
//      .addrsig
