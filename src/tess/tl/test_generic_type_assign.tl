struct Number a = val : a end
struct Point a = x : Number a, y : Number a end
struct Rect a = topleft : Point a, botright : Point a end

let main () =
  let r = Rect (Point (Number 9) (Number 1)) (Point (Number 2) (Number 3)) in
  let p = r.topleft in
  let num = p.x in
  let _ = num.val := 0 in
  let res = num.val in
  res
