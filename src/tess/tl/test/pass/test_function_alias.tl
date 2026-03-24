// Test function aliases: name = Module.func

#module Helper

add(a, b)    { a + b     }
add(a, b, c) { a + b + c }

#module main
#import <Print.tl>

my_add  = Helper.add
println = Print.println

apply(f, a, b)    { f(a, b)    }
apply(f, a, b, c) { f(a, b, c) }

main() {
    // Direct calls with different arities
    println(my_add(1, 2))
    println(my_add(1, 2, 3))

    // Function references via alias/N
    println(apply(my_add/2, 10, 20))
    println(apply(my_add/3, 10, 20, 30))

    // Variadic alias (already tested in test_variadic_print.tl, verify here too)
    println("done")

    0
}
