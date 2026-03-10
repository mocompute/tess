#module Shapes
Shape: | Circle { radius: Int } | Square { length: Int }

#module main
#alias S Shapes

main() {
    c            := S.Circle(1)
    result: CInt := when c {
        ci: Circle { ci.radius - 1 }
        sq: Square { 1 }
    }
    result
}
