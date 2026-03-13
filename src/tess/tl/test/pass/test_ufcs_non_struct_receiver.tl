// UFCS with non-struct receivers requires an explicit module qualifier.

#module mymath

square(n: Int) -> Int { n * n }
double(n: Int) -> Int { n + n }

#module main

main() {
    error := 0
    n := 5

    error = error + if n.mymath.square() == 25 { 0 } else { 1 }
    error = error + if n.mymath.double() == 10 { 0 } else { 1 }
    m := 3
    error = error + if m.mymath.square() == 9  { 0 } else { 1 }

    error
}
