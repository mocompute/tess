#module main

Shape: | Circle { radius: Int }
       | Square { length: Int }

main() {
    shape := Circle(3)

    // Mutable pattern matching - modify the radius
    when shape.& {
        c: Circle { c->radius = 5 }
        else { }
    }

    // Verify the mutation worked
    result := when shape {
        c: Circle { c.radius }
        s: Square { s.length }
    }

    // Should be 5, return 0 on success
    result - 5
}
