#module main
#import <string.tl>

main() {
    error := 0

    // Test null character
    c0 := '\0'
    error += if c0 == 0u {0} else {1}

    // Test newline
    c1 := '\n'
    error += if c1 == 10u {0} else {1}

    // Test carriage return
    c2 := '\r'
    error += if c2 == 13u {0} else {1}

    // Test tab
    c3 := '\t'
    error += if c3 == 9u {0} else {1}

    // Test backslash
    c4 := '\\'
    error += if c4 == 92u {0} else {1}

    // Test single quote
    c5 := '\''
    error += if c5 == 39u {0} else {1}

    // Test double quote
    c6 := '\"'
    error += if c6 == 34u {0} else {1}

    // Test alert/bell
    c7 := '\a'
    error += if c7 == 7u {0} else {1}

    // Test backspace
    c8 := '\b'
    error += if c8 == 8u {0} else {1}

    // Test form feed
    c9 := '\f'
    error += if c9 == 12u {0} else {1}

    // Test vertical tab
    c10 := '\v'
    error += if c10 == 11u {0} else {1}

    // Test escape sequences in strings
    s   := c"line1\nline2"
    len := c_strlen(s)
    error += if len == 11u {0} else {1}

    // Test multiple escapes in string
    s2   := c"a\tb\tc"
    len2 := c_strlen(s2)
    error += if len2 == 5u {0} else {1}

    error
}
