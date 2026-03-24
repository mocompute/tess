// Test cross-module variadic calls: Print.print and Print.println.

#module main
#import <Print.tl>

print   = Print.print
println = Print.println

main() {
    // Print.print outputs without newline
    print(42, c" hello ", true)

    // Print.println appends a newline
    println(c" world")

    // Zero args
    print()
    println()

    0
}
