#module main
#import <Array.tl>
#import <Str.tl>

main() {
    error := 0

    // 1. Same-module chain: Str → Str → Str
    s := Str.from_cstr(" HELLO ").trim().to_lower()
    error += if s.len() == 5zu {0} else {1}      // "hello"

    // 2. Cross-module: Str.split → Array[Str]
    parts := Str.from_cstr("a,b,c").split(Str.from_cstr(","))
    error += if parts.size == 3zu {0} else {1}
    error += if parts.get(0zu).len() == 1zu {0} else {1}   // "a".len() == 1

    // 3. Full round-trip chain: Str → Array → Str
    first_len := Str.from_cstr("hello,world").split(Str.from_cstr(",")).get(0zu).len()
    error += if first_len == 5zu {0} else {1}     // "hello".len() == 5

    // 4. Array methods after cross-module result
    words := Str.from_cstr("one,two,three").split(Str.from_cstr(","))
    words.push(Str.from_cstr("four"))              // Array.push via implicit module
    error += if words.size == 4zu {0} else {1}

    // cleanup via implicit module
    words.free()
    parts.free()

    error
}
