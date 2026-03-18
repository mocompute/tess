// CommandLine — Declarative command-line argument parsing.
//
// Builder pattern: create a CommandLine, add specs, then parse argc/argv.
// Results are queried via typed accessors on the Args value.
#module CommandLine
#import <Alloc.tl>
#import <Array.tl>
#import <HashMap.tl>
#import <String.tl>
#import <cstdlib.tl>
#import <cstdio.tl>
#import <cstring.tl>

Allocator = Alloc.Allocator

// Shorthand for string literal to String conversion.

// -- Types --

// What kind of argument a spec describes.
ArgKind: | FlagBool                     // --verbose, -v (no value)
         | FlagCount                    // -v -v -v (stackable counter)
         | FlagValue                    // --output FILE (takes a value)

// A single argument specification.
ArgSpec: {
    long_name:   String,
    short_name:  CChar,                 // '\0' for none
    description: String,
    kind:        ArgKind,
    required:    Bool,
}


// Individual parse error.
ParseError: | UnknownFlag     { flag: String }
            | MissingValue    { flag: String }
            | MissingRequired { name: String }
            | InvalidCommand  { name: String }

// Collection of all errors from a parse.
ParseErrors: { errors: Array[ParseError] }


// CommandLine specification (builder).
CommandLine: {
    _alloc:       Ptr[Allocator],
    _name:        String,
    _description: String,
    _specs:       Array[ArgSpec],
    _commands:    Array[String],
}

#module CommandLine.Args

// -- Types --

// Parsed value for a single argument.
ArgValue: | BoolVal  { v: Bool }
          | CountVal { v: Int    }
          | StrVal   { v: String }
          | Absent

// Parsed arguments container.
Args: {
    _values:     Ptr[HashMap[String, ArgValue]],
    _positional: Array[String],
    _rest:       Array[String],
    _command:    Option[String],
}

get_flag   (args: Ptr[Const[Args]], name: String) -> Bool
get_count  (args: Ptr[Const[Args]], name: String) -> Int
get_option (args: Ptr[Const[Args]], name: String) -> Option[String]
get_command(args: Ptr[Const[Args]])               -> Option[String]
positionals(args: Ptr[Const[Args]])               -> Array[String]
rest       (args: Ptr[Const[Args]])               -> Array[String]
has        (args: Ptr[Const[Args]], name: String) -> Bool

#module CommandLine

// -- Aliases --

ArgValue = Args.ArgValue

// -- Synopsis --

create (alloc: Ptr[Allocator], name: String, description: String) -> Ptr[CommandLine]
create (name: String, description: String)                        -> Ptr[CommandLine]
destroy(self: Ptr[CommandLine])                                   -> Void

flag    (self: Ptr[CommandLine], long_name: String, short_name: CChar, desc: String) -> Void
flag    (self: Ptr[CommandLine], long_name: String, desc: String)                    -> Void
counter (self: Ptr[CommandLine], long_name: String, short_name: CChar, desc: String) -> Void
option  (self: Ptr[CommandLine], long_name: String, short_name: CChar, desc: String) -> Void
option  (self: Ptr[CommandLine], long_name: String, desc: String)                    -> Void
required(self: Ptr[CommandLine], long_name: String, short_name: CChar, desc: String) -> Void
required(self: Ptr[CommandLine], long_name: String, desc: String)                    -> Void
command (self: Ptr[CommandLine], name: String)                                       -> Void

parse        (self: Ptr[CommandLine], argc: CInt, argv: Ptr[CString]) -> Result[Args, ParseErrors]
parse_or_exit(self: Ptr[CommandLine], argc: CInt, argv: Ptr[CString]) -> Args


help_text (self: Ptr[CommandLine]) -> String
print_help(self: Ptr[CommandLine]) -> Void

from_argv(argc: CInt, argv: Ptr[CString]) -> Array[String]

// -- Private helpers --

_find_spec_long (self: Ptr[CommandLine], name: String)                              -> Option[CSize]
_find_spec_short(self: Ptr[CommandLine], ch: CChar)                                 -> Option[CSize]
_store_bool     (values: Ptr[HashMap[String, ArgValue]], name: String)              -> Void
_store_count    (values: Ptr[HashMap[String, ArgValue]], name: String)              -> Void
_store_str      (values: Ptr[HashMap[String, ArgValue]], name: String, val: String) -> Void


// -- Implementation --


// -- Create / Destroy --

create(alloc, name, description) {
    p: Ptr[CommandLine] := alloc->malloc(alloc, sizeof[CommandLine]())
    if p == null { _tl_fatal_("CommandLine: oom") }
    p->_alloc       = alloc
    p->_name        = name
    p->_description = description
    p->_specs       = Array.empty[ArgSpec]()
    p->_commands    = Array.empty[String]()
    p
}

create(name, description) {
    create(Alloc.context.default, name, description)
}

destroy(self) {
    alloc := self->_alloc
    self->_specs.&.free()
    self->_commands.&.free()
    alloc->free(alloc, self)
}


// -- Spec builders --

flag(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagBool, required = false)
    self->_specs.&.push(spec)
}

flag(self, long_name: String, desc: String) {
    flag(self, long_name, '\0', desc)
}

counter(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagCount, required = false)
    self->_specs.&.push(spec)
}

option(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = false)
    self->_specs.&.push(spec)
}

option(self, long_name: String, desc: String) {
    option(self, long_name, '\0', desc)
}

required(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = true)
    self->_specs.&.push(spec)
}

required(self, long_name: String, desc: String) {
    required(self, long_name, '\0', desc)
}

command(self, name: String) {
    self->_commands.&.push(name)
}


// -- Private helpers --

_find_spec_long(self, name) {
    i: CSize := 0
    while i < self->_specs.size, i += 1 {
        if self->_specs.v.[i].long_name.eq(name) {
            return Some(i)
        }
    }
    None
}

_find_spec_short(self, ch) {
    if ch == '\0' { return None }
    i: CSize := 0
    while i < self->_specs.size, i += 1 {
        if self->_specs.v.[i].short_name == ch {
            return Some(i)
        }
    }
    None
}

_store_bool(values, name) {
    values.set(name, Args.BoolVal(v = true))
}

_store_count(values, name) {
    s: Some := values.get_copy(name) else {
        values.set(name, Args.CountVal(v = 1))
        return
    }
    when s.v {
        c: CountVal { values.set(name, Args.CountVal(v = c.v + 1)) }
        else { values.set(name, Args.CountVal(v = 1)) }
    }
}

_store_str(values, name, val) {
    values.set(name, Args.StrVal(v = val))
}

_parse_long(self, values, errors, name, arg, argc, argv, ip) {
    when name.index_of_char('=') {
        eq_pos: Some {
            key      := name.slice(0, eq_pos.v)
            val      := name.slice(eq_pos.v + 1, name.len())
            si: Some := _find_spec_long(self, key) else {
                Array.push(errors, UnknownFlag(flag = arg))
                return
            }
            spec := self->_specs.v.[si.v]
            when spec.kind {
                fv: FlagValue { _store_str(values, key, val) }
                else { Array.push(errors, UnknownFlag(flag = arg)) }
            }
        }
        n: None {
            si: Some := _find_spec_long(self, name) else {
                Array.push(errors, UnknownFlag(flag = arg))
                return
            }
            spec := self->_specs.v.[si.v]
            when spec.kind {
                fb: FlagBool { _store_bool(values, name) }
                fc: FlagCount { _store_count(values, name) }
                fv: FlagValue {
                    if ip.* + 1 < argc {
                        ip.* += 1
                        _store_str(values, name, String.from_cstr(argv.[ip.*]))
                    } else {
                        Array.push(errors, MissingValue(flag = name))
                    }
                }
            }
        }
    }
}

_parse_short(self, values, errors, arg, argc, argv, ip) {
    arg_len  := arg.len()
    j: CSize := 1
    while j < arg_len {
        bv: Some := String.byte_at(arg, j) else {
            j += 1
            continue
        }
        ch: CChar := bv.v
        si: Some  := _find_spec_short(self, ch) else {
            flag_str := s"-".cat(String.from_bytes(ch.&, 1))
            Array.push(errors, UnknownFlag(flag = flag_str))
            j += 1
            continue
        }
        spec := self->_specs.v.[si.v]
        when spec.kind {
            fb: FlagBool { _store_bool(values, spec.long_name) }
            fc: FlagCount { _store_count(values, spec.long_name) }
            fv: FlagValue {
                if j + 1 < arg_len {
                    val := arg.slice(j + 1, arg_len)
                    _store_str(values, spec.long_name, val)
                    j = arg_len
                } else if ip.* + 1 < argc {
                    ip.* = ip.* + 1
                    _store_str(values, spec.long_name, String.from_cstr(argv.[ip.*]))
                } else {
                    Array.push(errors, MissingValue(flag = spec.long_name))
                }
            }
        }
        j += 1
    }
}


// -- Parse --

parse(self, argc, argv) {
    values              := HashMap.create[String, ArgValue]()
    positional          := Array.empty[String]()
    rest_args           := Array.empty[String]()
    errors              := Array.empty[ParseError]()
    cmd: Option[String] := None

    dd           := s"--"
    short_prefix := s"-"

    i := 1; past_separator := false
    while i < argc {
        arg := String.from_cstr(argv.[i])

        // After --, everything is rest
        if past_separator {
            rest_args.&.push(arg)
            i += 1
            continue
        }

        // Check for --
        if arg.eq(dd) {
            past_separator = true
            i += 1
            continue
        }

        // Check for --long options
        if arg.starts_with(dd) {
            name := arg.slice(2, arg.len())
            _parse_long(self, values, errors.&, name, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Check for -x (short options)
        if arg.starts_with(short_prefix) && arg.len() > 1 {
            _parse_short(self, values, errors.&, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Not a flag — check for command or positional
        if self->_commands.size > 0 && positional.size == 0 {
            found_cmd := 0
            k: CSize  := 0
            while k < self->_commands.size, k += 1 {
                if self->_commands.v.[k].eq(arg) {
                    cmd       = Some(arg)
                    found_cmd = 1
                    break
                }
            }
            if found_cmd == 0 {
                positional.&.push(arg)
            }
        } else {
            positional.&.push(arg)
        }

        i += 1
    }

    // Check required args
    r: CSize := 0
    while r < self->_specs.size, r += 1 {
        spec := self->_specs.v.[r]
        if spec.required {
            if !values.contains(spec.long_name) {
                errors.&.push(MissingRequired(name = spec.long_name))
            }
        }
    }

    args := Args(_values = values, _positional = positional, _rest = rest_args, _command = cmd)

    if errors.size > 0 {
        Err(ParseErrors(errors = errors))
    } else {
        Ok(args)
    }
}

parse_or_exit(self, argc, argv) {
    result := parse(self, argc, argv)
    when result {
        ok:  Ok { ok.v }
        err: Err {
            i := 0
            while i < err.v.errors.size, i += 1 {
                when err.v.errors.v.[i] {
                    uf: UnknownFlag { c_printf("error: unknown flag '%s'\n", uf.flag.&.cstr()) }
                    mv: MissingValue { c_printf("error: missing value for '%s'\n", mv.flag.&.cstr()) }
                    mr: MissingRequired { c_printf("error: missing required option '%s'\n", mr.name.&.cstr()) }
                    ic: InvalidCommand { c_printf("error: unknown command '%s'\n", ic.name.&.cstr()) }
                }
            }
            c_printf("\n")
            print_help(self)
            c_exit(1)
            _tl_fatal_("unreachable")
        }
    }
}


// -- Help --

help_text(self) {
    result := s"Usage: ".cat(self->_name)
    result = result.cat(s" [options]")

    if self->_commands.size > 0 {
        result = result.cat(s" <command>")
    }

    result = result.cat(s" [args...]\n")

    if self->_description.len() > 0 {
        result = result.cat(s"\n")
        result = result.cat(self->_description)
        result = result.cat(s"\n")
    }

    if self->_specs.size > 0 {
        result = result.cat(s"\nOptions:\n")

        i: CSize := 0
        while i < self->_specs.size, i += 1 {
            spec := self->_specs.v.[i]
            result = result.cat(s"  ")

            if spec.short_name != '\0' {
                result = result.cat(s"-")
                result = result.cat(String.from_bytes(spec.short_name.&, 1))
                result = result.cat(s", ")
            } else {
                result = result.cat(s"    ")
            }

            result = result.cat(s"--")
            result = result.cat(spec.long_name)

            when spec.kind {
                fv: FlagValue { result = result.cat(s" <value>") }
                else { }
            }

            result = result.cat(s"\t")
            result = result.cat(spec.description)

            if spec.required {
                result = result.cat(s" (required)")
            }

            result = result.cat(s"\n")
        }
    }

    if self->_commands.size > 0 {
        result = result.cat(s"\nCommands:\n")
        j: CSize := 0
        while j < self->_commands.size, j += 1 {
            result = result.cat(s"  ")
            result = result.cat(self->_commands.v.[j])
            result = result.cat(s"\n")
        }
    }

    result
}

print_help(self) {
    txt := help_text(self)
    c_printf("%s", txt.&.cstr())
}


// -- Utility --

from_argv(argc, argv) {
    result := Array.empty[String]()
    i      := 0
    while i < argc, i += 1 {
        result.&.push(String.from_cstr(argv.[i]))
    }
    result
}


// -- Args --

#module CommandLine.Args

// -- Query --

get_flag(args, name) {
    s: Some := args->_values.get_copy(name) else { return false }
    when s.v {
        b: BoolVal { b.v }
        else { false }
    }
}

get_count(args, name) {
    s: Some := args->_values.get_copy(name) else { return 0 }
    when s.v {
        c: CountVal { c.v }
        else { 0 }
    }
}

get_option(args, name) {
    s: Some := args->_values.get_copy(name) else { return None }
    when s.v {
        sv: StrVal { Some(sv.v) }
        else { None }
    }
}

get_command(args)       { args->_command                        }
positionals(args)       { args->_positional                     }
rest       (args)       { args->_rest                           }
has        (args, name) { args->_values.contains(name) }
