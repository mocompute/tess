struct Point =
  x : int ;
  y : int ;
end


// Currently we have no global variables
let Point_empty () = Point 0 0

// Must annotate this wrapper functiohn because call to c_malloc is
// untyped.
let array_create n val : (int, Point) -> *Point =
  let sz = mul n (sizeof val) in
  let () = std_dbg "array_create total size = %u\n" sz in
  c_malloc sz

let main () =
  let () = std_dbg "Point_size = %i\n" (sizeof (Point 0 0)) in
  let ptr = array_create 4 (Point_empty ()) in
  let () = std_dbg "Created pointer %p\n" ptr in
  let () = c_free ptr in
  0
