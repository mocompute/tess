// User code cannot define a trait with a reserved compiler-provided trait name.

#module main

Add[T] : {
    add(a: T, b: T) -> T
}

main() { 0 }
