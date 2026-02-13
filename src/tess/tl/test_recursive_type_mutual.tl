#module main
#import <Array.tl>

Tree[a]:   { value: a, children: Ptr[Forest[a]] }
Forest[a]: { trees: Array.Array[Tree[a]]        }

main() {
    t1     := Tree[Int](value = 1, children = null)
    forest := Forest[Int](trees = Array.with_capacity[Tree[Int]](16))
    t1.children = forest.&
    0
}
