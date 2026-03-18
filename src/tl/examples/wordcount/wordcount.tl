// Word count — a CLI tool demonstrating CommandLine, HashMap, Array, File, and String.
//
// Build with:
//   tess exe -o wc wordcount.tl
//
// Or run directly:
//   tess run wordcount.tl -- [options] [files...]
//
// Examples:
//   tess run wordcount.tl -- file.tl                # lines, words, chars
//   tess run wordcount.tl -- -u -i file.tl          # + unique words, case-insensitive
//   tess run wordcount.tl -- -s -t 5 src/*.tl       # sort by words, top 5
//   echo "hello world" | tess run wordcount.tl      # read from stdin


#module main
#import <CommandLine.tl>
#import <File.tl>
#import <HashMap.tl>
#import <Array.tl>
#import <String.tl>
#import <cstdio.tl>
#import <cstdlib.tl>


// -- Types --

Counts: {
    path:         String,
    lines:        Int,
    words:        Int,
    chars:        Int,
    unique_words: Int,
}


// -- Helpers --

_is_space(c) {
    c == ' ' || c == '\t' || c == '\n' || c == '\r'
}

// Count lines, words, chars (and optionally unique words) in a string.
_count(content, path, track_unique, ignore_case) {
    n                 := String.len(content)
    lines             := 0
    words             := 0
    unique            := 0
    in_word           := false
    word_start: CSize := 0

    word_map := HashMap.create[String, Int]()

    i: CSize := 0
    while i < n, i += 1 {
        ch: Some := String.char_at(content, i) else { continue }
        c        := ch.v

        if c == '\n' { lines += 1 }

        if _is_space(c) {
            if in_word {
                words += 1
                if track_unique {
                    word := String.slice(content, word_start, i)
                    if ignore_case { word = String.to_lower(word) }
                    p := word_map.get(word)
                    if p != null {
                        p.* = p.* + 1
                    } else {
                        word_map.set(word, 1)
                        unique += 1
                    }
                }
                in_word = false
            }
        } else {
            if !in_word {
                word_start = i
                in_word    = true
            }
        }
    }

    // Last word (content may not end with whitespace).
    if in_word {
        words += 1
        if track_unique {
            word := String.slice(content, word_start, n)
            if ignore_case { word = String.to_lower(word) }
            p := word_map.get(word)
            if p != null {
                p.* = p.* + 1
            } else {
                word_map.set(word, 1)
                unique += 1
            }
        }
    }

    word_map.destroy()

    chars: Int := n
    Counts(path = path, lines = lines, words = words,
           chars = chars, unique_words = unique)
}

// Read all of stdin into a String.
_read_stdin() {
    buf := Array.empty[CChar]()

    while true {
        ch := c_fgetc(c_stdin)
        if ch == c_EOF { break }
        c: CChar := ch
        buf.&.push(c)
    }

    if buf.size == 0 {
        buf.&.free()
        return String.empty()
    }
    result := String.from_bytes(buf.v, buf.size)
    buf.&.free()
    result
}

// Print counts for one input.
_print_line(c, show_lines, show_words, show_chars, show_unique) {
    if show_lines { c_printf("%8d", c.lines) }
    if show_words { c_printf("%8d", c.words) }
    if show_chars { c_printf("%8d", c.chars) }
    if show_unique { c_printf("%8d", c.unique_words) }
    c_printf("  %s\n", String.cstr(c.path.&))
}


// -- Entry point --

main(argc: CInt, argv: Ptr[CString]) {
    cl := CommandLine.create(s"wc", s"Count lines, words, and characters in files")
    cl.flag(s"lines", 'l', s"Show line count")
    cl.flag(s"words", 'w', s"Show word count")
    cl.flag(s"chars", 'c', s"Show character/byte count")
    cl.flag(s"unique-words", 'u', s"Show unique word count")
    cl.flag(s"ignore-case", 'i', s"Case-insensitive unique words (use with -u)")
    cl.flag(s"sort", 's', s"Sort output by word count (descending)")
    cl.option(s"top", 't', s"Show only top N files")
    cl.flag(s"help", 'h', s"Show this help message")

    args := cl.parse_or_exit(argc, argv)

    if args.get_flag(s"help") {
        cl.print_help()
        cl.destroy()
        return 0
    }

    show_lines  := args.get_flag(s"lines")
    show_words  := args.get_flag(s"words")
    show_chars  := args.get_flag(s"chars")
    show_unique := args.get_flag(s"unique-words")
    ignore_case := args.get_flag(s"ignore-case")
    do_sort     := args.get_flag(s"sort")

    // Default: show all standard counts if no specific flag given.
    if !show_lines && !show_words && !show_chars && !show_unique {
        show_lines = true
        show_words = true
        show_chars = true
    }

    files   := args.positionals()
    results := Array.empty[Counts]()

    if files.size == 0 {
        // Read from stdin.
        content := _read_stdin()
        c       := _count(content, s"<stdin>", show_unique, ignore_case)
        results.&.push(c)
    } else {
        fi: CSize := 0
        while fi < files.size, fi += 1 {
            path := files.v.[fi]
            when File.read_file(path) {
                ok: Ok {
                    c := _count(ok.v, path, show_unique, ignore_case)
                    results.&.push(c)
                }
                err: Err {
                    when err.v {
                        nf: NotFound {
                            c_fprintf(c_stderr, "wc: %s: no such file or directory\n",
                                      String.cstr(nf.path.&))
                            void
                        }
                        pd: PermissionDenied {
                            c_fprintf(c_stderr, "wc: %s: permission denied\n",
                                      String.cstr(pd.path.&))
                            void
                        }
                        else {
                            c_fprintf(c_stderr, "wc: %s: read error\n",
                                      String.cstr(path.&))
                            void
                        }
                    }
                }
            }
        }
    }

    // Sort by word count descending.
    if do_sort {
        results.&.sort((a, b) { b.words - a.words })
    }

    // Apply --top N limit.
    when args.get_option(s"top") {
        s: Some {
            top_n := c_atoi(String.cstr(s.v.&))
            if top_n > 0 {
                limit: CSize := top_n
                if limit < results.size {
                    results = results.slice(0, limit)
                }
            }
        }
        n: None { }
    }

    // Print results.
    ri: CSize := 0
    while ri < results.size, ri += 1 {
        _print_line(results.v.[ri], show_lines, show_words, show_chars, show_unique)
    }

    // Print totals for multiple files.
    if results.size > 1 {
        total := Counts(path = s"total", lines = 0, words = 0,
                        chars = 0, unique_words = 0)
        ti: CSize := 0
        while ti < results.size, ti += 1 {
            total.lines += results.v.[ti].lines
            total.words += results.v.[ti].words
            total.chars += results.v.[ti].chars
            total.unique_words += results.v.[ti].unique_words
        }
        _print_line(total, show_lines, show_words, show_chars, show_unique)
    }

    cl.destroy()
    0
}
