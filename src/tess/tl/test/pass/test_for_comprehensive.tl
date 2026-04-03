#module main
#import <Array.tl>
#import <Slice.tl>
#import <String.tl>

// --- from test_for_statement_basic.tl ---
test_statement_basic() {
    xs := Array.init(0, 4zu)

    i := 1
    for x.& in xs {
        x.* = i
        i   = i + 1
    }

    sum := 0
    for x in xs {
        sum = sum + x
    }
    sum - 10
}

// --- from test_for_statement_module.tl ---
test_statement_module() {
    xs := Array.init_undefined[CSize](4zu)

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

// --- from test_for_break.tl ---
test_break() {
    xs: Ptr[Int] := c_malloc(10zu * sizeof[Int]())

    // Initialize array with values 0..9
    i := 0
    while i < 10, i += 1 {
        xs.[i] = i
    }

    // Find first value >= 5 and exit
    found := 0

    i := 0
    x := -1
    while i < 10, i += 1 {
        x = xs.[i]
        if x >= 5 {
            found = x
            break
        }
    }

    c_free(xs)

    // found should be 5
    result: CInt := found - 5
    result
}

// --- from test_for_continue.tl ---
test_continue() {
    xs := Array.init(0, 10zu)

    // Initialize array with values 0..9
    i := 0
    for x.& in xs {
        x.* = i
        i   = i + 1
    }

    // Sum values < 5, skip others
    sum := 0
    for x in xs {
        if x >= 5 {
            continue
        }
        sum = sum + x
    }
    // sum should be 0 + 1 + 2 + 3 + 4 = 10
    sum - 10
}

// --- from test_for_ufcs_slice.tl ---
test_ufcs_slice() {
    arr := Array.init(0, 4)
    i   := 1
    for x.& in arr {
        x.* = i
        i   = i + 1
    }

    s := arr.slice(0, 4)

    // Value iteration over Slice -- no explicit module needed
    sum := 0
    for x in s {
        sum = sum + x
    }

    if sum != 10 { 1 } else { 0 }
}

// --- from test_for_ufcs_string.tl ---
test_ufcs_string() {
    s     := "hello"
    count := 0
    for c in s {
        count = count + 1
    }
    if count != 5 { 1 } else { 0 }
}

main() {
    result := 0
    result = result + test_statement_basic()
    result = result + test_statement_module()
    result = result + test_break()
    result = result + test_continue()
    result = result + test_ufcs_slice()
    result = result + test_ufcs_string()
    result
}
