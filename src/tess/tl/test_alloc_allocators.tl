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

test_transient_realloc() {
  error := 0
  p1 := Alloc.transient_alloc(64)
  p2 := Alloc.transient_realloc(p1, 128)
  if p1 != p2 { error += 1 }
  error
}

test_bump_realloc_non_contiguous() {
  error := 0

  bump := Alloc.bump_create(Alloc.context.managed, 1024)

  p1 := bump->malloc(bump, 64)
  p2 := bump->malloc(bump, 16)
  p3 := bump->realloc(bump, p1, 128)

  if p1 == p2 { error += 1 }
  if p1 == p3 { error += 1 }

  Alloc.bump_destroy(bump)

  error
}

test_bump_free_reuse() {
  error := 0

  bump := Alloc.bump_create(Alloc.context.managed, 1024)

  p1 := bump->malloc(bump, 64)
  bump->free(bump, p1)
  p2 := bump->malloc(bump, 128)

  if p1 != p2 { error += 1 }

  Alloc.bump_destroy(bump)

  error
}

test_align_next_power_of_two() {
    error := 0

  error += if 1 == Alloc.next_power_of_two(0)      {0} else {1}
  error += if 1 == Alloc.next_power_of_two(1 << 0) {0} else {1}
  error += if 2 == Alloc.next_power_of_two(1 << 1) {0} else {1}
  error += if 4 == Alloc.next_power_of_two(3)      {0} else {1}
  error += if 4 == Alloc.next_power_of_two(4)      {0} else {1}
  error += if 8 == Alloc.next_power_of_two(5)      {0} else {1}

  error
}


main() {

  error := 0

  error += test_bump_create_destroy()
  error += test_bump_realloc()
  error += test_bump_realloc_non_contiguous()
  error += test_bump_free_reuse()
  error += test_align_next_power_of_two()

  error += test_transient_realloc()

  error

}
