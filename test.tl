let is_even n =
  if eq 0 n
  then true
  else is_odd (decr n)

let is_odd n =
  if eq 0 n
  then false
  else is_even (decr 1)

let main () =
  if is_even 10
  then 0
  else 1



// $ ./tess c test.tl | cc -O -x c - -S -masm=intel -o -
//
// main:                                   # @main
//      .cfi_startproc
// # %bb.0:
//      xor	eax, eax
//      ret
// .Lfunc_end0:
//      .size	main, .Lfunc_end0-main
//      .cfi_endproc
