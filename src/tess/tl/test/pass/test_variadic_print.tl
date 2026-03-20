// Test cross-module variadic calls: Print.print and Print.println.

#module main
#import <Print.tl>

main() {
    // Print.print outputs without newline
    Print.print(42, " hello ", true)

    // Print.println appends a newline
    Print.println(" world")

    // Zero args
    Print.print()
    Print.println()

    0
}
