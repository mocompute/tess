#module main
#import <File.tl>

test_dirname() {
    fails := 0

    d1 := File.Path.dirname("/foo/bar/baz.txt")
    if !String.eq(d1, "/foo/bar") { c_printf(c"FAIL dirname 1: %s\n", String.cstr(d1.&)) fails += 1 }

    d2 := File.Path.dirname("foo.txt")
    if !String.eq(d2, ".") { c_printf(c"FAIL dirname 2: %s\n", String.cstr(d2.&)) fails += 1 }

    d3 := File.Path.dirname("/foo")
    if !String.eq(d3, "/") { c_printf(c"FAIL dirname 3: %s\n", String.cstr(d3.&)) fails += 1 }

    d4 := File.Path.dirname("")
    if !String.eq(d4, ".") { c_printf(c"FAIL dirname 4: %s\n", String.cstr(d4.&)) fails += 1 }

    fails
}

test_basename() {
    fails := 0

    b1 := File.Path.basename("/foo/bar/baz.txt")
    if !String.eq(b1, "baz.txt") { c_printf(c"FAIL basename 1: %s\n", String.cstr(b1.&)) fails += 1 }

    b2 := File.Path.basename("foo.txt")
    if !String.eq(b2, "foo.txt") { c_printf(c"FAIL basename 2: %s\n", String.cstr(b2.&)) fails += 1 }

    b3 := File.Path.basename("/")
    if !String.eq(b3, "/") { c_printf(c"FAIL basename 3: %s\n", String.cstr(b3.&)) fails += 1 }

    fails
}

test_extension() {
    fails := 0

    e1       := File.Path.extension("foo.txt")
    s1: Some := e1 else { c_printf(c"FAIL ext 1: None\n") fails += 1 return fails }
    if !String.eq(s1.value, ".txt") { c_printf(c"FAIL ext 1: %s\n", String.cstr(s1.value.&)) fails += 1 }

    e2 := File.Path.extension("foo")
    when e2 { s: Some { c_printf(c"FAIL ext 2: expected None\n") fails += 1 } n: None { } }

    e3 := File.Path.extension(".hidden")
    when e3 { s: Some { c_printf(c"FAIL ext 3: expected None for .hidden\n") fails += 1 } n: None { } }

    e4       := File.Path.extension("archive.tar.gz")
    s4: Some := e4 else { c_printf(c"FAIL ext 4: None\n") fails += 1 return fails }
    if !String.eq(s4.value, ".gz") { c_printf(c"FAIL ext 4: %s\n", String.cstr(s4.value.&)) fails += 1 }

    fails
}

test_stem() {
    fails := 0

    s1 := File.Path.stem("foo.txt")
    if !String.eq(s1, "foo") { c_printf(c"FAIL stem 1: %s\n", String.cstr(s1.&)) fails += 1 }

    s2 := File.Path.stem("foo")
    if !String.eq(s2, "foo") { c_printf(c"FAIL stem 2: %s\n", String.cstr(s2.&)) fails += 1 }

    s3 := File.Path.stem(".hidden")
    if !String.eq(s3, ".hidden") { c_printf(c"FAIL stem 3: %s\n", String.cstr(s3.&)) fails += 1 }

    fails
}

test_join() {
    fails := 0

    j1 := File.Path.join("/foo", "bar.txt")
    if !String.eq(j1, "/foo/bar.txt") { c_printf(c"FAIL join 1: %s\n", String.cstr(j1.&)) fails += 1 }

    j2 := File.Path.join("/foo/", "bar.txt")
    if !String.eq(j2, "/foo/bar.txt") { c_printf(c"FAIL join 2: %s\n", String.cstr(j2.&)) fails += 1 }

    j3 := File.Path.join("", "bar.txt")
    if !String.eq(j3, "bar.txt") { c_printf(c"FAIL join 3: %s\n", String.cstr(j3.&)) fails += 1 }

    // Absolute file path should be returned as-is
    j4 := File.Path.join("/foo", "/bar.txt")
    if !String.eq(j4, "/bar.txt") { c_printf(c"FAIL join 4: %s\n", String.cstr(j4.&)) fails += 1 }

    // Empty file should return dir
    j5 := File.Path.join("/foo", "")
    if !String.eq(j5, "/foo") { c_printf(c"FAIL join 5: %s\n", String.cstr(j5.&)) fails += 1 }

    j6 := File.Path.join("bar", "")
    if !String.eq(j6, "bar") { c_printf(c"FAIL join 6: %s\n", String.cstr(j6.&)) fails += 1 }

    // Both empty
    j7 := File.Path.join("", "")
    if !String.eq(j7, "") { c_printf(c"FAIL join 7: %s\n", String.cstr(j7.&)) fails += 1 }

    fails
}

test_normalize() {
    fails := 0

    n1 := File.Path.normalize("a/../b/./c")
    if !String.eq(n1, "b/c") { c_printf(c"FAIL norm 1: %s\n", String.cstr(n1.&)) fails += 1 }

    n2 := File.Path.normalize("/a/b/../c")
    if !String.eq(n2, "/a/c") { c_printf(c"FAIL norm 2: %s\n", String.cstr(n2.&)) fails += 1 }

    n3 := File.Path.normalize("")
    if !String.is_empty(n3) { c_printf(c"FAIL norm 3: expected empty\n") fails += 1 }

    n4 := File.Path.normalize(".")
    if !String.eq(n4, ".") { c_printf(c"FAIL norm 4: %s\n", String.cstr(n4.&)) fails += 1 }

    n5 := File.Path.normalize("/")
    if !String.eq(n5, "/") { c_printf(c"FAIL norm 5: %s\n", String.cstr(n5.&)) fails += 1 }

    fails
}

test_is_absolute() {
    fails := 0

    if !File.Path.is_absolute("/foo") { c_printf(c"FAIL abs 1\n") fails += 1 }
    if File.Path.is_absolute("foo") { c_printf(c"FAIL abs 2\n") fails += 1 }
    if File.Path.is_absolute("") { c_printf(c"FAIL abs 3\n") fails += 1 }
    if !File.Path.is_relative("foo") { c_printf(c"FAIL rel 1\n") fails += 1 }
    if File.Path.is_relative("/foo") { c_printf(c"FAIL rel 2\n") fails += 1 }

    fails
}

test_relative() {
    fails := 0

    // Sibling directories
    r1: Ok := File.Path.relative("/a/b", "/a/c") else {
        c_printf(c"FAIL relative 1: error\n")
        return 1
    }
    if !String.eq(r1.value, "../c") { c_printf(c"FAIL relative 1: %s\n", r1.value.cstr()) fails += 1 }

    // Nested path
    r2: Ok := File.Path.relative("/a/b", "/a/b/c/d") else {
        c_printf(c"FAIL relative 2: error\n")
        return 1
    }
    if !String.eq(r2.value, "c/d") { c_printf(c"FAIL relative 2: %s\n", r2.value.cstr()) fails += 1 }

    // Same path
    r3: Ok := File.Path.relative("/a/b", "/a/b") else {
        c_printf(c"FAIL relative 3: error\n")
        return 1
    }
    if !String.eq(r3.value, ".") { c_printf(c"FAIL relative 3: %s\n", r3.value.cstr()) fails += 1 }

    // Deep divergence
    r4: Ok := File.Path.relative("/a/b/c", "/x/y") else {
        c_printf(c"FAIL relative 4: error\n")
        return 1
    }
    if !String.eq(r4.value, "../../../x/y") { c_printf(c"FAIL relative 4: %s\n", r4.value.cstr()) fails += 1 }

    // Empty input should return error
    r5 := File.Path.relative("", "/a")
    when r5 {
        ok: Ok { c_printf(c"FAIL relative 5: expected error\n") fails += 1 }
        err: Err { }
    }

    fails
}

main() {
    fails := 0

    fails += test_dirname()
    fails += test_basename()
    fails += test_extension()
    fails += test_stem()
    fails += test_join()
    fails += test_normalize()
    fails += test_is_absolute()
    fails += test_relative()

    if fails == 0 { c_printf(c"All path tests passed.\n") }
    else { c_printf(c"%d path test(s) failed.\n", fails) }

    fails
}
