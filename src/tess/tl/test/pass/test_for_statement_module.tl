#module main
#import <Array.tl>

main() {
    xs := Array.with_capacity_undefined[CSize](4zu)

    // Note: the for statement refers to the Array.Indexed module defined in <Array.tl>.
    // Iterator modules may define arbitrary fields on the iterator, such as `index` in this example.
    // By using `it.&` the caller signals its desire to get a mutable iterator.
    // See the section `Iterator interface` in <Array-tutorial.tl> for more documentation.
    for it.& in Array.Indexed xs {
        it.ptr.* = it.index + 1zu
    }

    sum := 0zu
    for it in Array.Indexed xs {
        sum = sum + it.value
    }
    // sum is CSize; main returns Int. Convert via comparison.
    if sum == 10zu {0} else {1}
}
