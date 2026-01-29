#module main

f(n) {
    if n <= 0 { n } else { f(n - 1) }
}

main() {
    f(100000)
}

// ./tess c src/tess/tl/test_tail_call.tl | cc -x c - -O -S -o - -masm=intel
//      .intel_syntax noprefix
//      .file	"-"
//      .text
//      .globl	main                            # -- Begin function main
//      .p2align	4
//      .type	main,@function
// main:                                   # @main
//      .cfi_startproc
// # %bb.0:
//      mov	edi, 100000
//      jmp	tl_fun_f_0                      # TAILCALL
// .Lfunc_end0:
//      .size	main, .Lfunc_end0-main
//      .cfi_endproc
//                                         # -- End function
//      .p2align	4                               # -- Begin function tl_fun_f_0
//      .type	tl_fun_f_0,@function
// tl_fun_f_0:                             # @tl_fun_f_0
//      .cfi_startproc
// # %bb.0:
//      mov	rax, rdi
//      test	rax, rax
//      jle	.LBB1_3
//      .p2align	4
// .LBB1_2:                                # =>This Inner Loop Header: Depth=1
//      dec	rax
//      test	rax, rax
//      jg	.LBB1_2
// .LBB1_3:
//      xor	edi, edi
//      ret
// .Lfunc_end1:
//      .size	tl_fun_f_0, .Lfunc_end1-tl_fun_f_0
//      .cfi_endproc
//                                         # -- End function
//      .ident	"clang version 20.1.8"
//      .section	".note.GNU-stack","",@progbits
//      .addrsig


// ./tess c src/tess/tl/test_tail_call.tl | cc -x c - -O2 -S -o - -masm=intel
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
