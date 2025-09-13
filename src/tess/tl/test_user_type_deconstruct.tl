struct Point =
  x : int ;
  y : int
end

let main () =
  let p = Point 1 2 in
  let (x = x, y = y) = p in
  decr x
