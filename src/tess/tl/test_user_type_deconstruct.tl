struct Point =
  x : int ;
  y : int
end

struct Rect =
  topleft  : Point ;
  botright : Point
end

let main () =
  let r = Rect (Point 1 2) (Point 3 4) in
  let (tl = topleft) = r in
  let (ret = x) = tl in
  decr ret
