#module Shapes
Shape : | Circle { radius: Int } | Square { length: Int }

#module main
#alias Shapes S

main() {
    c := S.Circle(1)
    result : CInt := when c {
        ci: Circle { ci.radius - 1 }
        sq: Square { 1 }
    }
}
