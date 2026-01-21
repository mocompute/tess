#module main
#import <stdio.tl>
#import <string.tl>

main() {
  error := 0

  // Test null character
  c0 := '\0'
  error = error + if c0 == 0 {0} else {1}

  // Test newline
  c1 := '\n'
  error = error + if c1 == 10 {0} else {1}

  // Test carriage return
  c2 := '\r'
  error = error + if c2 == 13 {0} else {1}

  // Test tab
  c3 := '\t'
  error = error + if c3 == 9 {0} else {1}

  // Test backslash
  c4 := '\\'
  error = error + if c4 == 92 {0} else {1}

  // Test single quote
  c5 := '\''
  error = error + if c5 == 39 {0} else {1}

  // Test double quote
  c6 := '\"'
  error = error + if c6 == 34 {0} else {1}

  // Test alert/bell
  c7 := '\a'
  error = error + if c7 == 7 {0} else {1}

  // Test backspace
  c8 := '\b'
  error = error + if c8 == 8 {0} else {1}

  // Test form feed
  c9 := '\f'
  error = error + if c9 == 12 {0} else {1}

  // Test vertical tab
  c10 := '\v'
  error = error + if c10 == 11 {0} else {1}

  // Test escape sequences in strings
  s := "line1\nline2"
  len := c_strlen(s)
  error = error + if len == 11 {0} else {1}

  // Test multiple escapes in string
  s2 := "a\tb\tc"
  len2 := c_strlen(s2)
  error = error + if len2 == 5 {0} else {1}

  c_printf("escape sequences test: %d errors\n", error)
  error
}
