// upgrade_strings — Upgrade Tess string literal prefixes.
//
// Transforms string prefixes to match new Tess convtions:
//   c"hello"  → c"hello"    (bare strings become explicit C strings)
//   "hello" → c"hello"     (s-prefix strings become bare strings)
//
// Build with:
//   tess exe -o upgrade_strings upgrade_strings.tl
//
// Or run directly:
//   tess run upgrade_strings.tl -- [-i] file1.tl [file2.tl ...]


#module main
#import <Array.tl>
#import <CommandLine.tl>
#import <File.tl>
#import <Print.tl>
#import <String.tl>
#import <cstdio.tl>
#import <cstdlib.tl>


// -- Character classification helpers --

_is_alpha(c) {
    (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
}

_is_digit(c) {
    c >= '0' && c <= '9'
}

_is_alnum_or_under(c) {
    _is_alpha(c) || _is_digit(c) || c == '_'
}


// -- Core transformation --

// Process a single line, transforming string prefixes.
// Returns the transformed line.
_process_line(line) {
    n   := line.len()
    out := Array.empty[CChar]()

    i: CSize := 0
    while i < n, i += 1 {
        ch: Some := line.char_at(i) else { continue }
        c        := ch.value

        // Character literal — skip through so '"' inside doesn't confuse us.
        if c == '\'' {
            out.push(c)
            i += 1
            // Handle escape inside char literal.
            inner: Some := line.char_at(i) else { continue }
            if inner.value == '\\' {
                out.push(inner.value)
                i += 1
                esc: Some := line.char_at(i) else { continue }
                out.push(esc.value)
                i += 1
            } else {
                out.push(inner.value)
                i += 1
            }
            // Closing quote.
            close: Some := line.char_at(i) else { continue }
            out.push(close.value)
            continue
        }

        // Found a double quote — start of a string literal.
        if c == '"' {
            // Determine if there is a prefix character.
            has_prefix    := false
            prefix: CChar := 0

            if out.size > 0 {
                prev := out.data.[out.size - 1]
                if _is_alpha(prev) {
                    // Check that the letter is standalone (not part of an identifier).
                    if out.size == 1 || !_is_alnum_or_under(out.data.[out.size - 2]) {
                        has_prefix = true
                        prefix     = prev
                    }
                }
            }

            if has_prefix {
                if prefix == 's' {
                    // Remove the s prefix — pop it from output.
                    out.pop()
                }
                // For c or any other prefix, leave as-is.
            } else {
                // No prefix — insert c.
                out.push('c')
            }

            // Copy the opening quote.
            out.push('"')
            i += 1

            // Copy string body until closing quote, handling escapes.
            while i < n {
                sc: Some := line.char_at(i) else { break }
                sv       := sc.value

                if sv == '\\' {
                    // Escape sequence — copy backslash and next char.
                    out.push(sv)
                    i += 1
                    nc: Some := line.char_at(i) else { break }
                    out.push(nc.value)
                    i += 1
                    continue
                }

                if sv == '"' {
                    // Closing quote.
                    out.push('"')
                    // Don't increment i here — the outer loop's i += 1 handles it.
                    break
                }

                out.push(sv)
                i += 1
            }
            continue
        }

        // Normal character outside any string.
        out.push(c)
    }

    // Build result string from the character array.
    if out.size == 0 {
        out.free()
        return String.empty()
    }
    result := String.from_bytes(out.data, out.size)
    out.free()
    result
}


// -- File processing --

_process_file(path, in_place) {
    when File.read_file(path) {
        ok: Ok {
            content := ok.value
            lines   := content.split("\n")

            output := Array.empty[String]()
            for line in lines {
                transformed := _process_line(line)
                output.push(transformed)
            }

            result := "\n".join(output)

            if in_place {
                when File.write_file(path, result) {
                    ok:  Ok { }
                    err: Err {
                        Print.fprintln(c_stderr, f"error: could not write {path.cstr()}")
                        return 1
                    }
                }
            } else {
                Print.print(f"{result}")
            }
            return 0
        }
        err: Err {
            when err.error {
                nf: NotFound {
                    Print.fprintln(c_stderr, f"error: {nf.path}: no such file")
                }
                pd: PermissionDenied {
                    Print.fprintln(c_stderr, f"error: {pd.path}: permission denied")
                }
                else {
                    Print.fprintln(c_stderr, c"error: {path}: read error")
                }
            }
            return 1
        }
    }
}


// -- Entry point --

main(argc: CInt, argv: Ptr[CString]) {
    cl := CommandLine.create("upgrade_strings",
                             "Upgrade Tess string literal prefixes")
    cl.flag("in-place", 'i', "Modify files in place")
    cl.flag("help", 'h', "Show this help message")

    args := cl.parse_or_exit(argc, argv)

    if args.get_flag("help") {
        cl.print_help()
        cl.destroy()
        return 0
    }

    in_place := args.get_flag("in-place")
    files    := args.positionals()

    if files.size == 0 {
        Print.fprintln(c_stderr, "error: no input files")
        cl.print_help()
        cl.destroy()
        return 1
    }

    errors := 0
    for file in files {
        errors += _process_file(file, in_place)
    }

    cl.destroy()
    errors
}
