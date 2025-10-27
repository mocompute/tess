_tl_sizeof_ (x : a) -> Int

sizeof(T) { _tl_sizeof_(T) }

: Point(a) {x:a, y:a }

main() {
  sizeof(Point(Int))
}



// _tl_sizeof_ (x : a) -> Int
// c_malloc(sz : Int) -> Ptr(a)
// c_free(ptr : Ptr(a)) -> Nil


// : Array(a) {
//   v:    Ptr(a)
//   size: Int
// }

// array_new(n : Int, x) {
//   // Array(Ptr(a), 0)
//   // Array {
//   //   v : Ptr(x) = c_malloc(n * _tl_sizeof_(x))
//   //   size = 0
//   // }
//   Array { v = x, size = 0 }
// }

// : Point { x : Int y : Int }

// main() {

//   array_new(8, Point(0, 0))

// }
