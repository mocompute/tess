struct foo =
    x : Int,
end

let main () =
    let a = foo 99 in
    let () = a.x := 0 in
    a.x
