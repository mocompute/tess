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
//   echo c"hello world" | tess run wordcount.tl      # read from stdin


#module main
#import <CommandLine.tl>
#import <File.tl>
#import <HashMap.tl>
#import <Array.tl>
#import <String.tl>
#import <Print.tl>
#import <cstdio.tl>
#import <cstdlib.tl>

println  = Print.println
fprintln = Print.fprintln


// -- Types --

Counts: {
    path:         String,
    lines:        Int,
    words:        Int,
    chars:        Int,
    unique_words: Int,
}


// -- Helpers --

is_space(c) {
    c == ' ' || c == '\t' || c == '\n' || c == '\r'
}

// Count lines, words, chars (and optionally unique words) in a string.
do_count(content, path, track_unique, ignore_case) {
    n          := content.len()
    lines      := 0
    words      := 0
    unique     := 0
    in_word    := false
    word_start := 0

    word_map := HashMap.create[String, Int]()
    defer word_map.destroy()

    record_word := (from, to) {
        words += 1
        if !track_unique { return }
        word := content.copy(from, to)
        if ignore_case { word = word.to_lower() }
        when word_map.get(word) {
            s: Some { s.value.* += 1 }
            n: None { word_map.set(word, 1); unique += 1 }
        }
    }

    i := 0
    while i < n, i += 1 {
        ch: Some := content.char_at(i) else { continue }
        c        := ch.value

        if c == '\n' { lines += 1 }

        if is_space(c) && in_word {
            record_word(word_start, i)
            in_word = false
        } else if !is_space(c) && !in_word {
            word_start = i
            in_word    = true
        }
    }

    // Last word (content may not end with whitespace).
    if in_word { record_word(word_start, n) }

    chars: Int := n
    Counts(path = path, lines = lines, words = words,
           chars = chars, unique_words = unique)
}

// Read all of stdin into a String.
read_stdin() {
    buf := Array.empty[CChar]()
    defer buf.free()

    while true {
        ch := c_fgetc(c_stdin)
        if ch == c_EOF { break }
        c: CChar := ch
        buf.push(c)
    }

    if buf.size == 0 { return String.empty() }
    String.from_bytes(buf.data, buf.size)
}

// Print counts for one input.
print_line(c, show_lines, show_words, show_chars, show_unique) {
    if show_lines { print(f"{c.lines:8d}") }
    if show_words { print(f"{c.words:8d}") }
    if show_chars { print(f"{c.chars:8d}") }
    if show_unique { print(f"{c.unique_words:8d}") }
    println(f"  {c.path}")
}

report_error(path, error) {
    when error {
        nf: NotFound { fprintln(c_stderr, f"wc: {nf.path}: no such file or directory") }
        pd: PermissionDenied { fprintln(c_stderr, f"wc: {pd.path}: permission denied") }
        else { fprintln(c_stderr, f"wc: {path}: read error") }
    }
}

// -- Entry point --

main(argc: CInt, argv: Ptr[CString]) {
    cl := CommandLine.create("wc", "Count lines, words, and characters in files")
    defer cl.destroy()
    cl.flag("lines", 'l', "Show line count")
    cl.flag("words", 'w', "Show word count")
    cl.flag("chars", 'c', "Show character/byte count")
    cl.flag("unique-words", 'u', "Show unique word count")
    cl.flag("ignore-case", 'i', "Case-insensitive unique words (use with -u)")
    cl.flag("sort", 's', "Sort output by word count (descending)")
    cl.option("top", 't', "Show only top N files")
    cl.flag("help", 'h', "Show this help message")

    args := cl.parse_or_exit(argc, argv)

    if args.get_flag("help") {
        cl.print_help()
        return 0
    }

    show_lines  := args.get_flag("lines")
    show_words  := args.get_flag("words")
    show_chars  := args.get_flag("chars")
    show_unique := args.get_flag("unique-words")
    ignore_case := args.get_flag("ignore-case")
    do_sort     := args.get_flag("sort")

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
        content := read_stdin()
        c       := do_count(content, "<stdin>", show_unique, ignore_case)
        results.push(c)
    } else {
        for path in files {
            ok: Ok := File.read_file(path) else err { report_error(path, err.error); continue }
            results.push(do_count(ok.value, path, show_unique, ignore_case))
        }
    }

    // Sort by word count descending.
    if do_sort {
        results.sort((a, b) { b.words - a.words })
    }

    // Apply --top N limit.
    if s: Some := args.get_option("top") {
        top_n := c_atoi(s.value.cstr())
        if top_n > 0 {
            limit: CSize := top_n
            if limit < results.size {
                results = results.copy(0, limit)
            }
        }
    }

    // Print results.
    for r in results {
        print_line(r, show_lines, show_words, show_chars, show_unique)
    }

    // Print totals for multiple files.
    if results.size > 1 {
        total := Counts(path = "total", lines = 0, words = 0,
                        chars = 0, unique_words = 0)
        for r in results {
            total.lines += r.lines
            total.words += r.words
            total.chars += r.chars
            total.unique_words += r.unique_words
        }
        print_line(total, show_lines, show_words, show_chars, show_unique)
    }

    0
}
