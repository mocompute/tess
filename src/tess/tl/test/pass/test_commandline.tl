#module main
#import <CommandLine.tl>
#import <String.tl>


test_basic_flag() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Enable verbose output")
    parser.flag("help", 'h', "Show help")

    argv: CArray[CString] := {
        c"test",
        c"--verbose",
        c"--help"
    }
    result := parser.parse(3, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.value.get_flag("verbose") { error += 1 }
    if !ok.value.get_flag("help") { error += 1 }
    if ok.value.get_flag("nonexistent") { error += 1 }

    parser.destroy()
    error
}

test_short_flag() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Enable verbose output")
    parser.flag("help", 'h', "Show help")

    argv: CArray[CString] := {
        c"test",
        c"-vh"
    }
    result := parser.parse(2, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.value.get_flag("verbose") { error += 1 }
    if !ok.value.get_flag("help") { error += 1 }

    parser.destroy()
    error
}

test_option_long() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"--output",
        c"foo.txt"
    }
    result := parser.parse(3, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_option("output") {
        s: Some { if s.value != "foo.txt" { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_eq() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"--output=bar.txt"
    }
    result := parser.parse(2, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_option("output") {
        s: Some { if s.value != "bar.txt" { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"-o",
        c"baz.txt"
    }
    result := parser.parse(3, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_option("output") {
        s: Some { if s.value != "baz.txt" { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_option_short_inline() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"-ofile.txt"
    }
    result := parser.parse(2, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_option("output") {
        s: Some { if s.value != "file.txt" { error += 1 } }
        n: None { error += 1 }
    }


    parser.destroy()
    error
}

test_counter() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.counter("verbose", 'v', "Verbosity level")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"-v",
        c"-v"
    }
    result := parser.parse(4, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.value.get_count("verbose") != 3 { error += 1 }

    parser.destroy()
    error
}

test_positional() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"file1.txt",
        c"file2.txt"
    }
    result := parser.parse(4, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    pos := ok.value.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if pos.data.[0] != "file1.txt" { error += 1 }
    }
    if pos.size >= 2 {
        if pos.data.[1] != "file2.txt" { error += 1 }
    }

    parser.destroy()
    error
}

test_double_dash() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"--",
        c"--not-a-flag",
        c"foo"
    }
    result := parser.parse(5, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.value.get_flag("verbose") { error += 1 }
    r := ok.value.rest()
    if r.size != 2 { error += 1 }
    if r.size >= 1 {
        if r.data.[0] != "--not-a-flag" { error += 1 }
    }
    if r.size >= 2 {
        if r.data.[1] != "foo" { error += 1 }
    }
    if ok.value.positionals().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_unknown_flag_error() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")

    argv: CArray[CString] := {
        c"test",
        c"--unknown"
    }
    result := parser.parse(2, argv)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.error.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_missing_value_error() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"--output"
    }
    result := parser.parse(2, argv)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.error.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_missing() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.required("config", 'c', "Config file path")

    argv: CArray[CString] := { c"test" }
    result                := parser.parse(1, argv)

    err: Err := result else { parser.destroy(), return error + 1 }

    if err.error.errors.size != 1 { error += 1 }

    parser.destroy()
    error
}

test_required_provided() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.required("config", 'c', "Config file path")

    argv: CArray[CString] := {
        c"test",
        c"--config",
        c"app.conf"
    }
    result := parser.parse(3, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_option("config") {
        s: Some { if s.value != "app.conf" { error += 1 } }
        n: None { error += 1 }
    }

    parser.destroy()
    error
}

test_command() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.command("build")
    parser.command("run")
    parser.flag("verbose", 'v', "Verbose")

    argv: CArray[CString] := {
        c"test",
        c"build",
        c"-v",
        c"file.txt"
    }
    result := parser.parse(4, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    when ok.value.get_command() {
        s: Some { if s.value != "build" { error += 1 } }
        n: None { error += 1 }
    }
    if !ok.value.get_flag("verbose") { error += 1 }
    pos := ok.value.positionals()
    if pos.size != 1 { error += 1 }
    if pos.size >= 1 {
        if pos.data.[0] != "file.txt" { error += 1 }
    }

    parser.destroy()
    error
}

test_has() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"-ofoo"
    }
    result := parser.parse(3, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.value.has("verbose") { error += 1 }
    if !ok.value.has("output") { error += 1 }
    if ok.value.has("missing") { error += 1 }

    parser.destroy()
    error
}

test_help_text() {
    error := 0

    parser := CommandLine.create("mytool", "A useful tool")
    parser.flag("verbose", 'v', "Enable verbose output")
    parser.option("output", 'o', "Output file path")
    parser.required("config", 'c', "Config file")

    txt := parser.help_text()
    if !String.contains(txt, "mytool") { error += 1 }
    if !String.contains(txt, "verbose") { error += 1 }
    if !String.contains(txt, "output") { error += 1 }
    if !String.contains(txt, "(required)") { error += 1 }

    parser.destroy()
    error
}

test_from_argv() {
    error := 0

    argv: CArray[CString] := {
        c"prog",
        c"arg1",
        c"arg2"
    }
    result := CommandLine.from_argv(3, argv)

    if result.size != 3 { error += 1 }
    if result.size >= 1 {
        if result.data.[0] != "prog" { error += 1 }
    }
    if result.size >= 2 {
        if result.data.[1] != "arg1" { error += 1 }
    }
    if result.size >= 3 {
        if result.data.[2] != "arg2" { error += 1 }
    }

    Array.free(result.&)
    error
}

test_empty_args() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")

    argv: CArray[CString] := { c"test" }
    result                := parser.parse(1, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if ok.value.get_flag("verbose") { error += 1 }
    if ok.value.positionals().size != 0 { error += 1 }
    if ok.value.rest().size != 0 { error += 1 }

    parser.destroy()
    error
}

test_mixed_flags_and_positionals() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Verbose")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"input.txt",
        c"--output=out.txt",
        c"extra.txt"
    }
    result := parser.parse(5, argv)

    ok: Ok := result else { parser.destroy(), return error + 1 }

    if !ok.value.get_flag("verbose") { error += 1 }
    when ok.value.get_option("output") {
        s: Some { if s.value != "out.txt" { error += 1 } }
        n: None { error += 1 }
    }
    pos := ok.value.positionals()
    if pos.size != 2 { error += 1 }
    if pos.size >= 1 {
        if pos.data.[0] != "input.txt" { error += 1 }
    }
    if pos.size >= 2 {
        if pos.data.[1] != "extra.txt" { error += 1 }
    }

    parser.destroy()
    error
}

test_parse_or_exit() {
    error := 0

    parser := CommandLine.create("test", "A test tool")
    parser.flag("verbose", 'v', "Enable verbose output")
    parser.option("output", 'o', "Output file")

    argv: CArray[CString] := {
        c"test",
        c"-v",
        c"--output",
        c"out.txt"
    }
    args := parser.parse_or_exit(4, argv)

    if !args.get_flag("verbose") { error += 1 }
    when args.get_option("output") {
        s: Some { if s.value != "out.txt" { error += 1 } }
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
