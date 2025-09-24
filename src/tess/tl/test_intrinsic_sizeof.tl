struct point = x : int, y : int end

let main () =
  let x = point 1 2 in
  let _ = std_dbg "sizeof a point struct is: %u\n" (_tl_sizeof_ x) in
  let _ = std_dbg "sizeof a point type is: %u\n" (_tl_sizeoft_ point) in
  0
