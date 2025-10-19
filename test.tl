struct Point a = x : a, y : a end
struct Rect a = topleft : Point a, botright : Point a end

let main () =
  let r = Rect (Point 1 2) (Point 3 4) in
  let p = r.topleft in
  let res = p.x in
  res
