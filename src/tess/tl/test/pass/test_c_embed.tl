#module main

#ifc
int add(int a, int b) { return a + b; }
#endc

// Required: type annotation for C functions
c_add(a: CInt, b: CInt) -> CInt

main() {
    result := c_add(-1, 1)
    result
}
