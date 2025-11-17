#module main
#import <stdlib.tl>

Point(a) : { x : a, y : a, next: Ptr(Point(a)) }

main() {

  p1 := Point(x = 1, y = 2, next = null)
  p2 := Point(x = 3, y = 4, next = p1.&)

  p2.next->x - 1
}
