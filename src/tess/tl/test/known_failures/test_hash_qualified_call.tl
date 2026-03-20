#module main
#import <Hash.tl>

main() {
    error := 0

    // Qualified call to trait function should work
    h1 := Hash.hash(42)
    error += if h1 != 0zu { 0 } else { 1 }

    // Should produce the same result as UFCS form
    x := 42
    h2 := x.hash()
    error += if h1 == h2 { 0 } else { 1 }

    error
}
