// Test cross-module variadic calls: Print.print and Print.println.

#module main
#import <Print.tl>

main() {
    // Print.print outputs without newline
    Print.print(42, c" hello ", true)

    // Print.println appends a newline
    Print.println(c" world")

    // Zero args
    Print.print()
    Print.println()

    0
}
