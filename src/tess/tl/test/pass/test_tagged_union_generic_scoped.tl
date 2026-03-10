#module main

MyOpt[a]: | MySome { v: a } | MyNone

main() {
    o1 := MyOpt.MySome(42)
    r  := when o1 { s: MySome { s.v } n: MyNone { 0 } }
    if r == 42 {0} else {1}
}
