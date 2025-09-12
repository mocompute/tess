let be_true x = true
let be_false x = false

let main () =
    if be_false 0
    then 1
    else if be_true 0
         then 0
         else 1
