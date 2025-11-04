#ifc
// C code goes here

int add(int a, int b) { return a + b; }

#endc

#module main

c_add(a: Int, b: Int) -> Int

main() {
  c_add(-1, 1)
}
