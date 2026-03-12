#module main

[[no_conform(Ord)]]
Node: { id: Int }


main() {
    error := 0

    // Exact match: Node has [[no_conform(Ord)]]
    error += if Node :: [[no_conform(Ord)]] { 0 } else { 1 }

    // General match: Node has [[no_conform]] (any argument)
    error += if Node :: [[no_conform]] { 0 } else { 1 }

    // Negative: Node does NOT have [[no_conform(Eq)]]
    error += if Node :: [[no_conform(Eq)]] { 1 } else { 0 }

    error
}
