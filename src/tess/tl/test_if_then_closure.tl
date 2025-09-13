let main () =
    let val = 123 in
    let ptr = &val in
    begin
        if gte val 1
        then begin
            // the thunk generated for this arm will capture references to ptr and val,
            // so that the pointer assignment modifies val in the outer scope
            ptr.* := 0
        end
        else 99
        ;                       // this ; closes the if then else expression within the outer begin/end block

        // here val is being accessed in the outer scope, to confirm that it has been set to 0
        val
    end
