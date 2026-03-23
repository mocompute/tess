#module main
apply (f) { f()       }
apply2(f) { apply(f)  }
apply3(f) { apply2(f) }

main() {
    val  := true
    afun := () { val }
    foo  := apply3(afun)
    if foo { 0 } else { 1 }
}
