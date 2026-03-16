// CommandLine — Declarative command-line argument parsing.
//
// Builder pattern: create a CommandLine, add specs, then parse argc/argv.
// Results are queried via typed accessors on the Args value.
#module CommandLine
#import <Alloc.tl>
#import <Array.tl>
#import <HashMap.tl>
#import <String.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <string.tl>

Allocator = Alloc.Allocator

// Shorthand for string literal to String conversion.
_S(s) { String.from_cstr(s) }

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
    Array.free(self->_specs.&)
    Array.free(self->_commands.&)
    alloc->free(alloc, self)
}


// -- Spec builders --

flag(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagBool, required = false)
    Array.push(self->_specs.&, spec)
}

flag(self, long_name: String, desc: String) {
    flag(self, long_name, '\0', desc)
}

counter(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagCount, required = false)
    Array.push(self->_specs.&, spec)
}

option(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = false)
    Array.push(self->_specs.&, spec)
}

option(self, long_name: String, desc: String) {
    option(self, long_name, '\0', desc)
}

required(self, long_name: String, short_name: CChar, desc: String) {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = true)
    Array.push(self->_specs.&, spec)
}

required(self, long_name: String, desc: String) {
    required(self, long_name, '\0', desc)
}

command(self, name: String) {
    Array.push(self->_commands.&, name)
}


// -- Private helpers --

_find_spec_long(self, name) {
    i: CSize := 0
    while i < self->_specs.size, i += 1 {
        if String.eq(self->_specs.v.[i].long_name, name) {
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
    HashMap.set(values, name, Args.BoolVal(v = true))
}

_store_count(values, name) {
    s: Some := HashMap.get_copy(values, name) else {
        HashMap.set(values, name, Args.CountVal(v = 1))
        return
    }
    when s.v {
        c: CountVal { HashMap.set(values, name, Args.CountVal(v = c.v + 1)) }
        else { HashMap.set(values, name, Args.CountVal(v = 1)) }
    }
}

_store_str(values, name, val) {
    HashMap.set(values, name, Args.StrVal(v = val))
}

_parse_long(self, values, errors, name, arg, argc, argv, ip) {
    when String.index_of_char(name, '=') {
        eq_pos: Some {
            key      := String.slice(name, 0, eq_pos.v)
            val      := String.slice(name, eq_pos.v + 1, String.len(name))
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
    arg_len  := String.len(arg)
    j: CSize := 1
    while j < arg_len {
        bv: Some := String.byte_at(arg, j) else {
            j += 1
            continue
        }
        ch: CChar := bv.v
        si: Some  := _find_spec_short(self, ch) else {
            flag_str := String.cat(_S("-"), String.from_bytes(ch.&, 1))
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
                    val := String.slice(arg, j + 1, arg_len)
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

    dd           := _S("--")
    short_prefix := _S("-")

    i := 1; past_separator := false
    while i < argc {
        arg := String.from_cstr(argv.[i])

        // After --, everything is rest
        if past_separator {
            Array.push(rest_args.&, arg)
            i += 1
            continue
        }

        // Check for --
        if String.eq(arg, dd) {
            past_separator = true
            i += 1
            continue
        }

        // Check for --long options
        if String.starts_with(arg, dd) {
            name := String.slice(arg, 2, String.len(arg))
            _parse_long(self, values, errors.&, name, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Check for -x (short options)
        if String.starts_with(arg, short_prefix) && String.len(arg) > 1 {
            _parse_short(self, values, errors.&, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Not a flag — check for command or positional
        if self->_commands.size > 0 && positional.size == 0 {
            found_cmd := 0
            k: CSize  := 0
            while k < self->_commands.size, k += 1 {
                if String.eq(self->_commands.v.[k], arg) {
                    cmd       = Some(arg)
                    found_cmd = 1
                    break
                }
            }
            if found_cmd == 0 {
                Array.push(positional.&, arg)
            }
        } else {
            Array.push(positional.&, arg)
        }

        i += 1
    }

    // Check required args
    r: CSize := 0
    while r < self->_specs.size, r += 1 {
        spec := self->_specs.v.[r]
        if spec.required {
            if !HashMap.contains(values, spec.long_name) {
                Array.push(errors.&, MissingRequired(name = spec.long_name))
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
                    uf: UnknownFlag { c_printf("error: unknown flag '%s'\n", String.cstr(uf.flag.&)) }
                    mv: MissingValue { c_printf("error: missing value for '%s'\n", String.cstr(mv.flag.&)) }
                    mr: MissingRequired { c_printf("error: missing required option '%s'\n", String.cstr(mr.name.&)) }
                    ic: InvalidCommand { c_printf("error: unknown command '%s'\n", String.cstr(ic.name.&)) }
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
    result := String.cat(_S("Usage: "), self->_name)
    result = String.cat(result, _S(" [options]"))

    if self->_commands.size > 0 {
        result = String.cat(result, _S(" <command>"))
    }

    result = String.cat(result, _S(" [args...]\n"))

    if String.len(self->_description) > 0 {
        result = String.cat(result, _S("\n"))
        result = String.cat(result, self->_description)
        result = String.cat(result, _S("\n"))
    }

    if self->_specs.size > 0 {
        result = String.cat(result, _S("\nOptions:\n"))

        i: CSize := 0
        while i < self->_specs.size, i += 1 {
            spec := self->_specs.v.[i]
            result = String.cat(result, _S("  "))

            if spec.short_name != '\0' {
                result = String.cat(result, _S("-"))
                result = String.cat(result, String.from_bytes(spec.short_name.&, 1))
                result = String.cat(result, _S(", "))
            } else {
                result = String.cat(result, _S("    "))
            }

            result = String.cat(result, _S("--"))
            result = String.cat(result, spec.long_name)

            when spec.kind {
                fv: FlagValue { result = String.cat(result, _S(" <value>")) }
                else { }
            }

            result = String.cat(result, _S("\t"))
            result = String.cat(result, spec.description)

            if spec.required {
                result = String.cat(result, _S(" (required)"))
            }

            result = String.cat(result, _S("\n"))
        }
    }

    if self->_commands.size > 0 {
        result = String.cat(result, _S("\nCommands:\n"))
        j: CSize := 0
        while j < self->_commands.size, j += 1 {
            result = String.cat(result, _S("  "))
            result = String.cat(result, self->_commands.v.[j])
            result = String.cat(result, _S("\n"))
        }
    }

    result
}

print_help(self) {
    txt := help_text(self)
    c_printf("%s", String.cstr(txt.&))
}


// -- Utility --

from_argv(argc, argv) {
    result := Array.empty[String]()
    i      := 0
    while i < argc, i += 1 {
        Array.push(result.&, String.from_cstr(argv.[i]))
    }
    result
}


// -- Args --

#module CommandLine.Args

// -- Query --

get_flag(args, name) {
    s: Some := HashMap.get_copy(args->_values, name) else { return false }
    when s.v {
        b: BoolVal { b.v }
        else { false }
    }
}

get_count(args, name) {
    s: Some := HashMap.get_copy(args->_values, name) else { return 0 }
    when s.v {
        c: CountVal { c.v }
        else { 0 }
    }
}

get_option(args, name) {
    s: Some := HashMap.get_copy(args->_values, name) else { return None }
    when s.v {
        sv: StrVal { Some(sv.v) }
        else { None }
    }
}

get_command(args)       { args->_command                        }
positionals(args)       { args->_positional                     }
rest       (args)       { args->_rest                           }
has        (args, name) { HashMap.contains(args->_values, name) }
