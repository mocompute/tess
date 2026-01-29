#ifc

int add(int a, int b) { return a + b; }

#endc

#module main

// Required: type annotation for C functions
c_add(a: Int, b: Int) -> Int

main() {
    c_add(-1, 1)
}
