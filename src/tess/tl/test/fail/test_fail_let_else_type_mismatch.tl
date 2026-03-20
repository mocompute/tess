#module MyOption

T[a]: | MySome { v: a }
      | MyNone

#module main

main() {
    val: MyOption.T[CInt] := MyOption.MyNone
    s:   MySome           := val else { 3.14 }
    0
}
