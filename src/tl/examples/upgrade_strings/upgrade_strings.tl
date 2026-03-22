// upgrade_strings — Upgrade Tess string literal prefixes.
//
// Transforms string prefixes to match new Tess conventions:
//   c"hello"  → c"hello"    (bare strings become explicit C strings)
//   "hello" → c"hello"     (s-prefix strings become bare strings)
//
// Build with:
//   tess exe -o upgrade_strings upgrade_strings.tl
//
// Or run directly:
//   tess run upgrade_strings.tl -- [-i] file1.tl [file2.tl ...]


#module main
#import <CommandLine.tl>
#import <File.tl>
#import <String.tl>
#import <Array.tl>
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
    n   := String.len(line)
    out := Array.empty[CChar]()

    i: CSize := 0
    while i < n, i += 1 {
        ch: Some := String.char_at(line, i) else { continue }
        c        := ch.v

        // Character literal — skip through so '"' inside doesn't confuse us.
        if c == '\'' {
            out.push(c)
            i += 1
            // Handle escape inside char literal.
            inner: Some := String.char_at(line, i) else { continue }
            if inner.v == '\\' {
                out.push(inner.v)
                i += 1
                esc: Some := String.char_at(line, i) else { continue }
                out.push(esc.v)
                i += 1
            } else {
                out.push(inner.v)
                i += 1
            }
            // Closing quote.
            close: Some := String.char_at(line, i) else { continue }
            out.push(close.v)
            continue
        }

        // Found a double quote — start of a string literal.
        if c == '"' {
            // Determine if there is a prefix character.
            has_prefix := false
            prefix: CChar := 0

            if out.size > 0 {
                prev := out.v.[out.size - 1]
                if _is_alpha(prev) {
                    // Check that the letter is standalone (not part of an identifier).
                    if out.size == 1 || !_is_alnum_or_under(out.v.[out.size - 2]) {
                        has_prefix = true
                        prefix = prev
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
                sc: Some := String.char_at(line, i) else { break }
                sv := sc.v

                if sv == '\\' {
                    // Escape sequence — copy backslash and next char.
                    out.push(sv)
                    i += 1
                    nc: Some := String.char_at(line, i) else { break }
                    out.push(nc.v)
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
    result := String.from_bytes(out.v, out.size)
    out.free()
    result
}


// -- File processing --

_process_file(path, in_place) {
    when File.read_file(path) {
        ok: Ok {
            content := ok.v
            lines   := String.split(content, "\n")

            output := Array.empty[String]()
            li: CSize := 0
            while li < lines.size, li += 1 {
                transformed := _process_line(lines.v.[li])
                output.push(transformed)
            }

            result := String.join("\n", output)

            if in_place {
                when File.write_file(path, result) {
                    ok: Ok { }
                    err: Err {
                        c_fprintf(c_stderr, c"error: could not write %s\n",
                                  String.cstr(path.&))
                        return 1
                    }
                }
            } else {
                c_printf(c"%s", String.cstr(result.&))
                void
            }
            return 0
        }
        err: Err {
            when err.v {
                nf: NotFound {
                    c_fprintf(c_stderr, c"error: %s: no such file\n",
                              String.cstr(nf.path.&))
                    void
                }
                pd: PermissionDenied {
                    c_fprintf(c_stderr, c"error: %s: permission denied\n",
                              String.cstr(pd.path.&))
                    void
                }
                else {
                    c_fprintf(c_stderr, c"error: %s: read error\n",
                              String.cstr(path.&))
                    void
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
        c_fprintf(c_stderr, c"error: no input files\n")
        cl.print_help()
        cl.destroy()
        return 1
    }

    errors := 0
    fi: CSize := 0
    while fi < files.size, fi += 1 {
        errors += _process_file(files.v.[fi], in_place)
    }

    cl.destroy()
    errors
}
