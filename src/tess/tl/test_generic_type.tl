struct Point a = x : a, y : a end
struct Rect a = topleft : Point a, botright : Point a end

let main () =
  let r = Rect (Point 0 1) (Point 2 3) in
  let p = r.topleft in
  let res = p.x in
  res
