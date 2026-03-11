#module main
#import <string.tl>

main() {
    error := 0

    // Test null character
    c0 := '\0'
    error += if c0 == '\0' {0} else {1}

    // Test newline
    c1 := '\n'
    error += if c1 == '\n' {0} else {1}

    // Test carriage return
    c2 := '\r'
    error += if c2 == '\r' {0} else {1}

    // Test tab
    c3 := '\t'
    error += if c3 == '\t' {0} else {1}

    // Test backslash
    c4 := '\\'
    error += if c4 == '\\' {0} else {1}

    // Test single quote
    c5 := '\''
    error += if c5 == '\'' {0} else {1}

    // Test double quote
    c6 := '\"'
    error += if c6 == '\"' {0} else {1}

    // Test alert/bell
    c7 := '\a'
    error += if c7 == '\a' {0} else {1}

    // Test backspace
    c8 := '\b'
    error += if c8 == '\b' {0} else {1}

    // Test form feed
    c9 := '\f'
    error += if c9 == '\f' {0} else {1}

    // Test vertical tab
    c10 := '\v'
    error += if c10 == '\v' {0} else {1}

    // Test escape sequences in strings
    s   := "line1\nline2"
    len := c_strlen(s)
    error += if len == 11zu {0} else {1}

    // Test multiple escapes in string
    s2   := c"a\tb\tc"
    len2 := c_strlen(s2)
    error += if len2 == 5zu {0} else {1}

    error
}
