#module main

main() {
    error := 0

    // Nested CArray literal
    mat: CArray[CArray[Int, 2], 3] := {{1, 2}, {3, 4}, {5, 6}}
    error += if mat.[0].[0] == 1 {0} else {1}
    error += if mat.[0].[1] == 2 {0} else {1}
    error += if mat.[1].[0] == 3 {0} else {1}
    error += if mat.[1].[1] == 4 {0} else {1}
    error += if mat.[2].[0] == 5 {0} else {1}
    error += if mat.[2].[1] == 6 {0} else {1}

    return error
}
