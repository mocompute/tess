#module main
#import <File.tl>

test_dirname() {
    fails := 0

    d1 := File.Path.dirname(s"/foo/bar/baz.txt")
    if !String.eq(d1, s"/foo/bar") { c_printf("FAIL dirname 1: %s\n", String.cstr(d1.&)) fails += 1 }

    d2 := File.Path.dirname(s"foo.txt")
    if !String.eq(d2, s".") { c_printf("FAIL dirname 2: %s\n", String.cstr(d2.&)) fails += 1 }

    d3 := File.Path.dirname(s"/foo")
    if !String.eq(d3, s"/") { c_printf("FAIL dirname 3: %s\n", String.cstr(d3.&)) fails += 1 }

    d4 := File.Path.dirname(s"")
    if !String.eq(d4, s".") { c_printf("FAIL dirname 4: %s\n", String.cstr(d4.&)) fails += 1 }

    fails
}

test_basename() {
    fails := 0

    b1 := File.Path.basename(s"/foo/bar/baz.txt")
    if !String.eq(b1, s"baz.txt") { c_printf("FAIL basename 1: %s\n", String.cstr(b1.&)) fails += 1 }

    b2 := File.Path.basename(s"foo.txt")
    if !String.eq(b2, s"foo.txt") { c_printf("FAIL basename 2: %s\n", String.cstr(b2.&)) fails += 1 }

    b3 := File.Path.basename(s"/")
    if !String.eq(b3, s"/") { c_printf("FAIL basename 3: %s\n", String.cstr(b3.&)) fails += 1 }

    fails
}

test_extension() {
    fails := 0

    e1       := File.Path.extension(s"foo.txt")
    s1: Some := e1 else { c_printf("FAIL ext 1: None\n") fails += 1 return fails }
    if !String.eq(s1.v, s".txt") { c_printf("FAIL ext 1: %s\n", String.cstr(s1.v.&)) fails += 1 }

    e2 := File.Path.extension(s"foo")
    when e2 { s: Some { c_printf("FAIL ext 2: expected None\n") fails += 1 } n: None { } }

    e3 := File.Path.extension(s".hidden")
    when e3 { s: Some { c_printf("FAIL ext 3: expected None for .hidden\n") fails += 1 } n: None { } }

    e4       := File.Path.extension(s"archive.tar.gz")
    s4: Some := e4 else { c_printf("FAIL ext 4: None\n") fails += 1 return fails }
    if !String.eq(s4.v, s".gz") { c_printf("FAIL ext 4: %s\n", String.cstr(s4.v.&)) fails += 1 }

    fails
}

test_stem() {
    fails := 0

    s1 := File.Path.stem(s"foo.txt")
    if !String.eq(s1, s"foo") { c_printf("FAIL stem 1: %s\n", String.cstr(s1.&)) fails += 1 }

    s2 := File.Path.stem(s"foo")
    if !String.eq(s2, s"foo") { c_printf("FAIL stem 2: %s\n", String.cstr(s2.&)) fails += 1 }

    s3 := File.Path.stem(s".hidden")
    if !String.eq(s3, s".hidden") { c_printf("FAIL stem 3: %s\n", String.cstr(s3.&)) fails += 1 }

    fails
}

test_join() {
    fails := 0

    j1 := File.Path.join(s"/foo", s"bar.txt")
    if !String.eq(j1, s"/foo/bar.txt") { c_printf("FAIL join 1: %s\n", String.cstr(j1.&)) fails += 1 }

    j2 := File.Path.join(s"/foo/", s"bar.txt")
    if !String.eq(j2, s"/foo/bar.txt") { c_printf("FAIL join 2: %s\n", String.cstr(j2.&)) fails += 1 }

    j3 := File.Path.join(s"", s"bar.txt")
    if !String.eq(j3, s"bar.txt") { c_printf("FAIL join 3: %s\n", String.cstr(j3.&)) fails += 1 }

    // Absolute file path should be returned as-is
    j4 := File.Path.join(s"/foo", s"/bar.txt")
    if !String.eq(j4, s"/bar.txt") { c_printf("FAIL join 4: %s\n", String.cstr(j4.&)) fails += 1 }

    fails
}

test_normalize() {
    fails := 0

    n1 := File.Path.normalize(s"a/../b/./c")
    if !String.eq(n1, s"b/c") { c_printf("FAIL norm 1: %s\n", String.cstr(n1.&)) fails += 1 }

    n2 := File.Path.normalize(s"/a/b/../c")
    if !String.eq(n2, s"/a/c") { c_printf("FAIL norm 2: %s\n", String.cstr(n2.&)) fails += 1 }

    n3 := File.Path.normalize(s"")
    if !String.is_empty(n3) { c_printf("FAIL norm 3: expected empty\n") fails += 1 }

    n4 := File.Path.normalize(s".")
    if !String.eq(n4, s".") { c_printf("FAIL norm 4: %s\n", String.cstr(n4.&)) fails += 1 }

    n5 := File.Path.normalize(s"/")
    if !String.eq(n5, s"/") { c_printf("FAIL norm 5: %s\n", String.cstr(n5.&)) fails += 1 }

    fails
}

test_is_absolute() {
    fails := 0

    if !File.Path.is_absolute(s"/foo") { c_printf("FAIL abs 1\n") fails += 1 }
    if File.Path.is_absolute(s"foo") { c_printf("FAIL abs 2\n") fails += 1 }
    if File.Path.is_absolute(s"") { c_printf("FAIL abs 3\n") fails += 1 }
    if !File.Path.is_relative(s"foo") { c_printf("FAIL rel 1\n") fails += 1 }
    if File.Path.is_relative(s"/foo") { c_printf("FAIL rel 2\n") fails += 1 }

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

    if fails == 0 { c_printf("All path tests passed.\n") }
    else { c_printf("%d path test(s) failed.\n", fails) }

    fails
}
