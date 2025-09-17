let main () =
  let val = 123 in
  let ptr = &val in

  let _ =
    if   gte val 1
    then ptr.* := 0 // (see Note 1)
    else 1          // this value is ignored
    in

  let _ = () in     // (see Note 2)
    val


// Note 1: the thunk generated for this arm will capture references to
// ptr and val, so that the pointer assignment modifies val in the
// outer scope.

// Note 2: this is a simple expression that simply evaluates to val.
// Note that val now has the value 0.
