#module main

early_return_if(x: Int) -> Int {
    if x > 0 {
        return 1
    }
    0
}

always_returns(x: Int) -> Int {
    return x
    99
}

conditional_return(x: Int) -> Int {
    if x == 1 { return 10 }
    if x == 2 { return 20 }
    if x == 3 { return 30 }
    0
}

main() {
    error := 0

    // Basic return skips subsequent code
    error += if always_returns(0) == 0 {0} else {1}
    error += if always_returns(42) == 42 {0} else {1}

    // Conditional return
    error += if early_return_if(5) == 1 {0} else {1}
    error += if early_return_if(-1) == 0 {0} else {1}
    error += if early_return_if(0) == 0 {0} else {1}

    // Multiple conditional returns
    error += if conditional_return(1) == 10 {0} else {1}
    error += if conditional_return(2) == 20 {0} else {1}
    error += if conditional_return(3) == 30 {0} else {1}
    error += if conditional_return(99) == 0 {0} else {1}

    error
}
