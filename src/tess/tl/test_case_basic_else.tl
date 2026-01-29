#module main

main() {
    n   := 12
    res := case n {
        0 { 1 }
        1 { 1 }
        2 { 2 }
        3 { 3 }
        else { 0 }
    }
    res
}
