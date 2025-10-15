struct Point a = x : a, y : a end
// struct Rect = topleft : Point, botright : Point end

let id x = x

let main () =
  id (1, 2, 3)
