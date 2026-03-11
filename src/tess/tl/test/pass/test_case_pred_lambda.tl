#module main
#import <string.tl>

main() {
    s1 := "hello"
    case s1, (a, b) { 0 == c_strcmp(a, b) } {
        "world" { 1 }
        "there" { 1 }
        "hello" { 0 }
    }
}


// ./tess c src/tess/tl/test_case_pred_lambda.tl | cc -O -x c - -S -masm=intel -o -
//      .intel_syntax noprefix
//      .file	"-"
//      .text
//      .globl	main                            # -- Begin function main
//      .p2align	4
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
//      .ident	"clang version 20.1.8"
//      .section	".note.GNU-stack","",@progbits
//      .addrsig
