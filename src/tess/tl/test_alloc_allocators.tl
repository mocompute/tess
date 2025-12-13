#module main
#import <Alloc.tl>

test_bump_create_destroy() {
  bump := Alloc.bump_create(Alloc.context.managed, 1024)
  Alloc.bump_destroy(bump)
  0
}

main() {

  error := 0

  error += test_bump_create_destroy()

  error
}
