let fact x =
    // confirmed that this will be tail call optimised by clang at -O
    if   lte x 1
    then 1
    else mul x (fact (decr x))

let main () =
    if   eq (fact 20) 2432902008176640000
    then 0
    else 1
