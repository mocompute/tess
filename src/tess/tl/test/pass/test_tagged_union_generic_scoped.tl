#module main

MyOpt[a]: | MySome { value: a } | MyNone

main() {
    o1 := MyOpt.MySome(42)
    r  := when o1 { s: MySome { s.value } n: MyNone { 0 } }
    if r == 42 {0} else {1}
}
