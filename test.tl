#module main
#import <Alloc.tl>

main() {
  bump := Alloc.bump_create(Alloc.context.managed, 4096)
  0
}
