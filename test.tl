struct Point a = x : a, y : a end
struct Rect a = topleft : Point a, botright : Point a end

let id x = x

let main () =
  Rect (Point 1 2) (Point 3 4)
