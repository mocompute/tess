#module main
#import <CommandLine.tl>
#import <String.tl>

S(s) { String.from_cstr(s) }

test_basic_flag() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Enable verbose output"))
    parser.flag(S(c"help"), 'h', S(c"Show help"))

    argv: CArray[CString, 3] := void
    argv.[0] = c"test"
    argv.[1] = c"--verbose"
    argv.[2] = c"--help"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S(c"verbose")) { error += 1 }
    if !ok.v.get_flag(S(c"help")) { error += 1 }
    if ok.v.get_flag(S(c"nonexistent")) { error += 1 }

    parser.destroy()
    error
}

test_short_flag() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Enable verbose output"))
    parser.flag(S(c"help"), 'h', S(c"Show help"))

    argv: CArray[CString, 2] := void
    argv.[0] = c"test"
    argv.[1] = c"-vh"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S(c"verbose")) { error += 1 }
    if !ok.v.get_flag(S(c"help")) { error += 1 }

    parser.destroy()
    error
}

test_option_long() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = c"test"
    argv.[1] = c"--output"
    argv.[2] = c"foo.txt"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"foo.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_eq() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = c"test"
    argv.[1] = c"--output=bar.txt"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"bar.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = c"test"
    argv.[1] = c"-o"
    argv.[2] = c"baz.txt"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"baz.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short_inline() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = c"test"
    argv.[1] = c"-ofile.txt"
    result := parser.parse(2, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"file.txt")) { error += 1 } }
        n: None { error += 1 }
    }


    parser.destroy()
    error
}

test_counter() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.counter(S(c"verbose"), 'v', S(c"Verbosity level"))

    argv: CArray[CString, 4] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"-v"
    argv.[3] = c"-v"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.v.get_count(S(c"verbose")) != 3 { error += 1 }

    parser.destroy()
    error
}

test_positional() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"file1.txt"
    argv.[3] = c"file2.txt"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    pos := ok.v.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S(c"file1.txt")) { error += 1 }
    }
    if pos.size >= 2 {
        if !String.eq(pos.v.[1], S(c"file2.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

test_double_dash() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))

    argv: CArray[CString, 5] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"--"
    argv.[3] = c"--not-a-flag"
    argv.[4] = c"foo"
    result := parser.parse(5, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S(c"verbose")) { error += 1 }
    r := ok.v.rest()
    if r.size != 2 { error += 1 }
    if r.size >= 1 {
        if !String.eq(r.v.[0], S(c"--not-a-flag")) { error += 1 }
    }
    if r.size >= 2 {
        if !String.eq(r.v.[1], S(c"foo")) { error += 1 }
    }
    if ok.v.positionals().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_unknown_flag_error() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))

    argv: CArray[CString, 2] := void
    argv.[0] = c"test"
    argv.[1] = c"--unknown"
    result := parser.parse(2, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_missing_value_error() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = c"test"
    argv.[1] = c"--output"
    result := parser.parse(2, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_missing() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.required(S(c"config"), 'c', S(c"Config file path"))

    argv: CArray[CString, 1] := void
    argv.[0] = c"test"
    result := parser.parse(1, argv.[0].&)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.v.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_provided() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.required(S(c"config"), 'c', S(c"Config file path"))

    argv: CArray[CString, 3] := void
    argv.[0] = c"test"
    argv.[1] = c"--config"
    argv.[2] = c"app.conf"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_option(S(c"config")) {
        s: Some { if !String.eq(s.v, S(c"app.conf")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_command() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.command(S(c"build"))
    parser.command(S(c"run"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = c"test"
    argv.[1] = c"build"
    argv.[2] = c"-v"
    argv.[3] = c"file.txt"
    result := parser.parse(4, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.v.get_command() {
        s: Some { if !String.eq(s.v, S(c"build")) { error += 1 } }
        n: None { error += 1 }
    }
    if !ok.v.get_flag(S(c"verbose")) { error += 1 }
    pos := ok.v.positionals()
    if pos.size != 1 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S(c"file.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

test_has() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"-ofoo"
    result := parser.parse(3, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.has(S(c"verbose")) { error += 1 }
    if !ok.v.has(S(c"output")) { error += 1 }
    if ok.v.has(S(c"missing")) { error += 1 }

    parser.destroy()
    error
}

test_help_text() {
    error := 0

    parser := CommandLine.create(S(c"mytool"), S(c"A useful tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Enable verbose output"))
    parser.option(S(c"output"), 'o', S(c"Output file path"))
    parser.required(S(c"config"), 'c', S(c"Config file"))

    txt := parser.help_text()
    if !String.contains(txt, S(c"mytool")) { error += 1 }
    if !String.contains(txt, S(c"verbose")) { error += 1 }
    if !String.contains(txt, S(c"output")) { error += 1 }
    if !String.contains(txt, S(c"(required)")) { error += 1 }

    parser.destroy()
    error
}

test_from_argv() {
    error := 0

    argv: CArray[CString, 3] := void
    argv.[0] = c"prog"
    argv.[1] = c"arg1"
    argv.[2] = c"arg2"
    result := CommandLine.from_argv(3, argv.[0].&)

    if result.size != 3 { error += 1 }
    if result.size >= 1 {
        if !String.eq(result.v.[0], S(c"prog")) { error += 1 }
    }
    if result.size >= 2 {
        if !String.eq(result.v.[1], S(c"arg1")) { error += 1 }
    }
    if result.size >= 3 {
        if !String.eq(result.v.[2], S(c"arg2")) { error += 1 }
    }

    Array.free(result.&)
    error
}

test_empty_args() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))

    argv: CArray[CString, 1] := void
    argv.[0] = c"test"
    result := parser.parse(1, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.v.get_flag(S(c"verbose")) { error += 1 }
    if ok.v.positionals().size != 0 { error += 1 }
    if ok.v.rest().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_mixed_flags_and_positionals() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Verbose"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 5] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"input.txt"
    argv.[3] = c"--output=out.txt"
    argv.[4] = c"extra.txt"
    result := parser.parse(5, argv.[0].&)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.v.get_flag(S(c"verbose")) { error += 1 }
    when ok.v.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"out.txt")) { error += 1 } }
        n: None { error += 1 }
    }
    pos := ok.v.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if !String.eq(pos.v.[0], S(c"input.txt")) { error += 1 }
    }
    if pos.size >= 2 {
        if !String.eq(pos.v.[1], S(c"extra.txt")) { error += 1 }
    }

    parser.destroy()
    error
}

test_parse_or_exit() {
    error := 0

    parser := CommandLine.create(S(c"test"), S(c"A test tool"))
    parser.flag(S(c"verbose"), 'v', S(c"Enable verbose output"))
    parser.option(S(c"output"), 'o', S(c"Output file"))

    argv: CArray[CString, 4] := void
    argv.[0] = c"test"
    argv.[1] = c"-v"
    argv.[2] = c"--output"
    argv.[3] = c"out.txt"
    args := parser.parse_or_exit(4, argv.[0].&)

    if !args.get_flag(S(c"verbose")) { error += 1 }
    when args.get_option(S(c"output")) {
        s: Some { if !String.eq(s.v, S(c"out.txt")) { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

_run(name, f) {
    r := f()
    if r != 0 { c_printf(c"FAIL: %s (%d)\n", name, r) }
    r
}

main() {
    error := 0
    error += _run(c"basic_flag", test_basic_flag/0)
    error += _run(c"short_flag", test_short_flag/0)
    error += _run(c"option_long", test_option_long/0)
    error += _run(c"option_eq", test_option_eq/0)
    error += _run(c"option_short", test_option_short/0)
    error += _run(c"option_short_inline", test_option_short_inline/0)
    error += _run(c"counter", test_counter/0)
    error += _run(c"positional", test_positional/0)
    error += _run(c"double_dash", test_double_dash/0)
    error += _run(c"unknown_flag_error", test_unknown_flag_error/0)
    error += _run(c"missing_value_error", test_missing_value_error/0)
    error += _run(c"required_missing", test_required_missing/0)
    error += _run(c"required_provided", test_required_provided/0)
    error += _run(c"command", test_command/0)
    error += _run(c"has", test_has/0)
    error += _run(c"help_text", test_help_text/0)
    error += _run(c"from_argv", test_from_argv/0)
    error += _run(c"empty_args", test_empty_args/0)
    error += _run(c"mixed", test_mixed_flags_and_positionals/0)
    error += _run(c"parse_or_exit", test_parse_or_exit/0)
    error
}
