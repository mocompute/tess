#module main
#import <Cmdline.tl>
#import <Str.tl>

S(s: Ptr[Const[CChar]]) -> Str.Str { Str.from_cstr(s) }

test_basic_flag() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Enable verbose output"))
    Cmdline.flag(parser, S("help"), 'h', S("Show help"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--verbose"
    argv.[2] = "--help"
    result := Cmdline.parse(parser, 3, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if !Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            if !Cmdline.get_flag(ok.v, S("help")) { error += 1 }
            if Cmdline.get_flag(ok.v, S("nonexistent")) { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_short_flag() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Enable verbose output"))
    Cmdline.flag(parser, S("help"), 'h', S("Show help"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "-vh"
    result := Cmdline.parse(parser, 2, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if !Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            if !Cmdline.get_flag(ok.v, S("help")) { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_option_long() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--output"
    argv.[2] = "foo.txt"
    result := Cmdline.parse(parser, 3, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_option(ok.v, S("output")) {
                s: Some { if !Str.eq(s.v, S("foo.txt")) { error += 1 } }
                n: None { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_option_eq() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--output=bar.txt"
    result := Cmdline.parse(parser, 2, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_option(ok.v, S("output")) {
                s: Some { if !Str.eq(s.v, S("bar.txt")) { error += 1 } }
                n: None { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_option_short() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "-o"
    argv.[2] = "baz.txt"
    result := Cmdline.parse(parser, 3, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_option(ok.v, S("output")) {
                s: Some { if !Str.eq(s.v, S("baz.txt")) { error += 1 } }
                n: None { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_option_short_inline() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "-ofile.txt"
    result := Cmdline.parse(parser, 2, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_option(ok.v, S("output")) {
                s: Some { if !Str.eq(s.v, S("file.txt")) { error += 1 } }
                n: None { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_counter() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.counter(parser, S("verbose"), 'v', S("Verbosity level"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "-v"
    argv.[3] = "-v"
    result := Cmdline.parse(parser, 4, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if Cmdline.get_count(ok.v, S("verbose")) != 3 { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_positional() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "file1.txt"
    argv.[3] = "file2.txt"
    result := Cmdline.parse(parser, 4, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            pos := Cmdline.positionals(ok.v)
            if pos.size != 2zu { error += 1 }
            if pos.size >= 1zu {
                if !Str.eq(pos.v.[0], S("file1.txt")) { error += 1 }
            }
            if pos.size >= 2zu {
                if !Str.eq(pos.v.[1], S("file2.txt")) { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_double_dash() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 5] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "--"
    argv.[3] = "--not-a-flag"
    argv.[4] = "foo"
    result := Cmdline.parse(parser, 5, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if !Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            r := Cmdline.rest(ok.v)
            if r.size != 2zu { error += 1 }
            if r.size >= 1zu {
                if !Str.eq(r.v.[0], S("--not-a-flag")) { error += 1 }
            }
            if r.size >= 2zu {
                if !Str.eq(r.v.[1], S("foo")) { error += 1 }
            }
            if Cmdline.positionals(ok.v).size != 0zu { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_unknown_flag_error() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--unknown"
    result := Cmdline.parse(parser, 2, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok { error += 1 }
        err: Err {
            if err.v.errors.size != 1zu { error += 1 }
        }
    }

    Cmdline.destroy(parser)
    error
}

test_missing_value_error() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 2] := void
    argv.[0] = "test"
    argv.[1] = "--output"
    result := Cmdline.parse(parser, 2, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok { error += 1 }
        err: Err {
            if err.v.errors.size != 1zu { error += 1 }
        }
    }

    Cmdline.destroy(parser)
    error
}

test_required_missing() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.required(parser, S("config"), 'c', S("Config file path"))

    argv: CArray[CString, 1] := void
    argv.[0] = "test"
    result := Cmdline.parse(parser, 1, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok { error += 1 }
        err: Err {
            if err.v.errors.size != 1zu { error += 1 }
        }
    }

    Cmdline.destroy(parser)
    error
}

test_required_provided() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.required(parser, S("config"), 'c', S("Config file path"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "--config"
    argv.[2] = "app.conf"
    result := Cmdline.parse(parser, 3, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_option(ok.v, S("config")) {
                s: Some { if !Str.eq(s.v, S("app.conf")) { error += 1 } }
                n: None { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_command() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.command(parser, S("build"))
    Cmdline.command(parser, S("run"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 4] := void
    argv.[0] = "test"
    argv.[1] = "build"
    argv.[2] = "-v"
    argv.[3] = "file.txt"
    result := Cmdline.parse(parser, 4, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            when Cmdline.get_command(ok.v) {
                s: Some { if !Str.eq(s.v, S("build")) { error += 1 } }
                n: None { error += 1 }
            }
            if !Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            pos := Cmdline.positionals(ok.v)
            if pos.size != 1zu { error += 1 }
            if pos.size >= 1zu {
                if !Str.eq(pos.v.[0], S("file.txt")) { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_has() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 3] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "-ofoo"
    result := Cmdline.parse(parser, 3, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if !Cmdline.has(ok.v, S("verbose")) { error += 1 }
            if !Cmdline.has(ok.v, S("output"))  { error += 1 }
            if Cmdline.has(ok.v, S("missing"))  { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_help_text() {
    error := 0

    parser := Cmdline.create(S("mytool"), S("A useful tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Enable verbose output"))
    Cmdline.option(parser, S("output"), 'o', S("Output file path"))
    Cmdline.required(parser, S("config"), 'c', S("Config file"))

    txt := Cmdline.help_text(parser)
    if !Str.contains(txt, S("mytool")) { error += 1 }
    if !Str.contains(txt, S("verbose")) { error += 1 }
    if !Str.contains(txt, S("output")) { error += 1 }
    if !Str.contains(txt, S("(required)")) { error += 1 }

    Cmdline.destroy(parser)
    error
}

test_from_argv() {
    error := 0

    argv: CArray[CString, 3] := void
    argv.[0] = "prog"
    argv.[1] = "arg1"
    argv.[2] = "arg2"
    result := Cmdline.from_argv(3, argv.[0].&)

    if result.size != 3zu { error += 1 }
    if result.size >= 1zu {
        if !Str.eq(result.v.[0], S("prog")) { error += 1 }
    }
    if result.size >= 2zu {
        if !Str.eq(result.v.[1], S("arg1")) { error += 1 }
    }
    if result.size >= 3zu {
        if !Str.eq(result.v.[2], S("arg2")) { error += 1 }
    }

    Array.free(result.&)
    error
}

test_empty_args() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))

    argv: CArray[CString, 1] := void
    argv.[0] = "test"
    result := Cmdline.parse(parser, 1, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            if Cmdline.positionals(ok.v).size != 0zu { error += 1 }
            if Cmdline.rest(ok.v).size != 0zu { error += 1 }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

test_mixed_flags_and_positionals() {
    error := 0

    parser := Cmdline.create(S("test"), S("A test tool"))
    Cmdline.flag(parser, S("verbose"), 'v', S("Verbose"))
    Cmdline.option(parser, S("output"), 'o', S("Output file"))

    argv: CArray[CString, 5] := void
    argv.[0] = "test"
    argv.[1] = "-v"
    argv.[2] = "input.txt"
    argv.[3] = "--output=out.txt"
    argv.[4] = "extra.txt"
    result := Cmdline.parse(parser, 5, argv.[0].&)

    case result: Result[Cmdline.Args, Cmdline.ParseErrors] {
        ok: Ok {
            if !Cmdline.get_flag(ok.v, S("verbose")) { error += 1 }
            when Cmdline.get_option(ok.v, S("output")) {
                s: Some { if !Str.eq(s.v, S("out.txt")) { error += 1 } }
                n: None { error += 1 }
            }
            pos := Cmdline.positionals(ok.v)
            if pos.size != 2zu { error += 1 }
            if pos.size >= 1zu {
                if !Str.eq(pos.v.[0], S("input.txt")) { error += 1 }
            }
            if pos.size >= 2zu {
                if !Str.eq(pos.v.[1], S("extra.txt")) { error += 1 }
            }
        }
        err: Err { error += 1 }
    }

    Cmdline.destroy(parser)
    error
}

_run(name: Ptr[Const[CChar]], f: () -> Int) -> Int {
    r := f()
    if r != 0 { c_printf("FAIL: %s (%d)\n", name, r) }
    r
}

main() -> CInt {
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
    ret: CInt := error
    ret
}
