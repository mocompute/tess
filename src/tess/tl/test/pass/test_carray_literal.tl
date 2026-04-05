#module main

main() {
    error := 0

    // Basic static literal
    arr: CArray[Int, 3] := {1, 2, 3}
    error += if arr.[0] == 1 {0} else {1}
    error += if arr.[1] == 2 {0} else {1}
    error += if arr.[2] == 3 {0} else {1}

    // Runtime expressions
    x := 10
    arr2: CArray[Int, 3] := {x, x + 1, x * 2}
    error += if arr2.[0] == 10 {0} else {1}
    error += if arr2.[1] == 11 {0} else {1}
    error += if arr2.[2] == 20 {0} else {1}

    // Float array
    farr: CArray[Float, 2] := {1.5, 2.5}
    error += if farr.[0] == 1.5 {0} else {1}
    error += if farr.[1] == 2.5 {0} else {1}

    // Const array
    carr: Const[CArray[Int, 3]] := {10, 20, 30}
    error += if carr.[0] == 10 {0} else {1}
    error += if carr.[1] == 20 {0} else {1}
    error += if carr.[2] == 30 {0} else {1}

    // Single element
    single: CArray[Int, 1] := {42}
    error += if single.[0] == 42 {0} else {1}

    return error
}
