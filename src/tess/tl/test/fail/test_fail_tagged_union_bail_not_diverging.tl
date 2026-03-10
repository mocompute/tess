#module MyOption

T[a]: | MySome { v: a }
      | MyNone

#module main

main() {
    val       := MyOption.MySome(42)
    s: MySome := val else { 0 }
    s.v
}
