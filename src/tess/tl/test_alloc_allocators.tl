#module main
#import <Alloc.tl>

test_bump_create_destroy() {
  bump := Alloc.bump_create(Alloc.context.managed, 1024)
  Alloc.bump_destroy(bump)
  0
}

test_bump_realloc() {
  error := 0

  bump := Alloc.bump_create(Alloc.context.managed, 1024)
  p1 := bump->malloc(bump, 64)
  p2 := bump->realloc(bump, p1, 128)
  if p1 != p2 { error += 1 }
  Alloc.bump_destroy(bump)
  error
}

main() {

  error := 0

  error += test_bump_create_destroy()
  error += test_bump_realloc()

  error

}
