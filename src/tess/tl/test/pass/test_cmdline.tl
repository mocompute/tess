#module main
#import <CommandLine.tl>
#import <String.tl>

S(s) { String.from_cstr(s) }

test_basic_flag() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Enable verbose output"))
    parser.flag(S("help"), 'h', S("Show help"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--verbose"
    argv.[2] = "--help"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S("verbose")) { error += 1 }
    if !ok.v.get_flag(S("help")) { error += 1 }
    if ok.v.get_flag(S("nonexistent")) { error += 1 }

    parser.destroy()
    error
}

test_short_flag() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Enable verbose output"))
    parser.flag(S("help"), 'h', S("Show help"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "-vh"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S("verbose")) { error += 1 }
    if !ok.v.get_flag(S("help")) { error += 1 }

    parser.destroy()
    error
}

test_option_long() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--output"
    argv.[2] = "foo.txt"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S("output")) {
        s: Some { if !String.eq(s.v, S("foo.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_eq() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--output=bar.txt"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S("output")) {
        s: Some { if !String.eq(s.v, S("bar.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "-o"
    argv.[2] = "baz.txt"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S("output")) {
        s: Some { if !String.eq(s.v, S("baz.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short_inline() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "-ofile.txt"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S("output")) {
        s: Some { if !String.eq(s.v, S("file.txt")) { error += 1 } }
        n: None { error += 1 }
    }


    parser.destroy()
    error
}

test_counter() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.counter(S("verbose"), 'v', S("Verbosity level"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "-v"
    argv.[3] = "-v"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.v.get_count(S("verbose")) != 3 { error += 1 }

    parser.destroy()
    error
}

test_positional() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "file1.txt"
    argv.[3] = "file2.txt"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    pos := ok.v.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S("file1.txt")) { error += 1 }
    }
    if pos.size >= 2 {
        if !String.eq(pos.v.[1], S("file2.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

test_double_dash() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 5] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "--"
    argv.[3] = "--not-a-flag"
    argv.[4] = "foo"
    result := parser.parse(5, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S("verbose")) { error += 1 }
    r := ok.v.rest()
    if r.size != 2 { error += 1 }
    if r.size >= 1 {
        if !String.eq(r.v.[0], S("--not-a-flag")) { error += 1 }
    }
    if r.size >= 2 {
        if !String.eq(r.v.[1], S("foo")) { error += 1 }
    }
    if ok.v.positionals().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_unknown_flag_error() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--unknown"
    result := parser.parse(2, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_missing_value_error() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--output"
    result := parser.parse(2, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_missing() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.required(S("config"), 'c', S("Config file path"))

    argv: CArray[CString, 1] := void
    argv.[0] = "test"
    result := parser.parse(1, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_provided() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.required(S("config"), 'c', S("Config file path"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--config"
    argv.[2] = "app.conf"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S("config")) {
        s: Some { if !String.eq(s.v, S("app.conf")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_command() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.command(S("build"))
    parser.command(S("run"))
    parser.flag(S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "build"
    argv.[2] = "-v"
    argv.[3] = "file.txt"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_command() {
        s: Some { if !String.eq(s.v, S("build")) { error += 1 } }
        n: None { error += 1 }
    }
    if !ok.v.get_flag(S("verbose")) { error += 1 }
    pos := ok.v.positionals()
    if pos.size != 1 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S("file.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

test_has() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "-ofoo"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.has(S("verbose")) { error += 1 }
    if !ok.v.has(S("output")) { error += 1 }
    if ok.v.has(S("missing")) { error += 1 }

    parser.destroy()
    error
}

test_help_text() {
    error := 0

    parser := CommandLine.create(S("mytool"), S("A useful tool"))
    parser.flag(S("verbose"), 'v', S("Enable verbose output"))
    parser.option(S("output"), 'o', S("Output file path"))
    parser.required(S("config"), 'c', S("Config file"))

    txt := parser.help_text()
    if !String.contains(txt, S("mytool")) { error += 1 }
    if !String.contains(txt, S("verbose")) { error += 1 }
    if !String.contains(txt, S("output")) { error += 1 }
    if !String.contains(txt, S("(required)")) { error += 1 }

    parser.destroy()
    error
}

test_from_argv() {
    error := 0

    argv: CArray[CString, 3] := void
    argv.[0] = "prog"
    argv.[1] = "arg1"
    argv.[2] = "arg2"
    result := CommandLine.from_argv(3, argv.[0].&)

    if result.size != 3 { error += 1 }
    if result.size >= 1 {
        if !String.eq(result.v.[0], S("prog")) { error += 1 }
    }
    if result.size >= 2 {
        if !String.eq(result.v.[1], S("arg1")) { error += 1 }
    }
    if result.size >= 3 {
        if !String.eq(result.v.[2], S("arg2")) { error += 1 }
    }

    Array.free(result.&)
    error
}

test_empty_args() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 1] := void
    argv.[0] = "test"
    result := parser.parse(1, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.v.get_flag(S("verbose")) { error += 1 }
    if ok.v.positionals().size != 0 { error += 1 }
    if ok.v.rest().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_mixed_flags_and_positionals() {
    error := 0

    parser := CommandLine.create(S("test"), S("A test tool"))
    parser.flag(S("verbose"), 'v', S("Verbose"))
    parser.option(S("output"), 'o', S("Output file"))

    argv: CArray[CString, 5] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "input.txt"
    argv.[3] = "--output=out.txt"
    argv.[4] = "extra.txt"
    result := parser.parse(5, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S("verbose")) { error += 1 }
    when ok.v.get_option(S("output")) {
        s: Some { if !String.eq(s.v, S("out.txt")) { error += 1 } }
        n: None { error += 1 }
    }
    pos := ok.v.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S("input.txt")) { error += 1 }
    }
    if pos.size >= 2 {
        if !String.eq(pos.v.[1], S("extra.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

_run(name, f) {
    r := f()
    if r != 0 { c_printf("FAIL: %s (%d)\n", name, r) }
    r
}

main() {
    error := 0
    error += _run("basic_flag", test_basic_flag/0)
    error += _run("short_flag", test_short_flag/0)
    error += _run("option_long", test_option_long/0)
    error += _run("option_eq", test_option_eq/0)
    error += _run("option_short", test_option_short/0)
    error += _run("option_short_inline", test_option_short_inline/0)
    error += _run("counter", test_counter/0)
    error += _run("positional", test_positional/0)
    error += _run("double_dash", test_double_dash/0)
    error += _run("unknown_flag_error", test_unknown_flag_error/0)
    error += _run("missing_value_error", test_missing_value_error/0)
    error += _run("required_missing", test_required_missing/0)
    error += _run("required_provided", test_required_provided/0)
    error += _run("command", test_command/0)
    error += _run("has", test_has/0)
    error += _run("help_text", test_help_text/0)
    error += _run("from_argv", test_from_argv/0)
    error += _run("empty_args", test_empty_args/0)
    error += _run("mixed", test_mixed_flags_and_positionals/0)
    error
}
