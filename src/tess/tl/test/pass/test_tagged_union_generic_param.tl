#module main

// renamed due to new conflict with builtin Some/None
T[a]: | MySome { value: a }
      | MyNone

my_unwrap(opt: T[Int]) -> Int {
    when opt {
        s: MySome { s.value }
        n: MyNone { 0 }
    }
}

main() {
    val          := MySome(42)
    result: CInt := my_unwrap(val) - 42
    result
}
