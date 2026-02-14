#module main

// renamed due to new conflict with builtin Some/None
T[a]: | MySome { v: a }
      | MyNone

my_unwrap(opt: T[Int]) -> Int {
    when opt {
        s: MySome { s.v }
        n: MyNone { 0 }
    }
}

main() {
    val := MySome(42)
    my_unwrap(val) - 42
}
