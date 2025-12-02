#module main

#ifc
struct foo { int x, y; };

struct foo make_foo(int x, int y) {
  return (struct foo){.x = x, .y = y};
}
#endc

// order of fields does not matter: it's only to annotate the C struct types
c_struct_foo : {y: CInt, x: CInt }

c_make_foo(x: CInt, y: CInt) -> c_struct_foo

main() {
  val := c_make_foo(1, 2)
  val.y - val.x - 1
}
