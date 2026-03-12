#module main

Foo: { tag: Byte }

extract(f: Foo) -> CSize {
    raw: CSize := f.tag, (raw & 0xF0zu) >> 4zu
}

main() {
    f := Foo(tag = 0xA5u)
    r := extract(f)
    if r == 10zu { 0 } else { 1 }
}
