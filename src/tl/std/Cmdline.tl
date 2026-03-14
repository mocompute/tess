// Cmdline — Declarative command-line argument parsing.
//
// Builder pattern: create a Parser, add specs, then parse argc/argv.
// Results are queried via typed accessors on the Args value.
#module Cmdline
#import <Alloc.tl>
#import <Array.tl>
#import <HashMap.tl>
#import <Str.tl>
#import <stdlib.tl>
#import <stdio.tl>
#import <string.tl>

Allocator = Alloc.Allocator

// Shorthand for string literal to Str conversion.
_S(s: Ptr[Const[CChar]]) -> Str.Str { Str.from_cstr(s) }

// -- Types --

// What kind of argument a spec describes.
ArgKind: | FlagBool     // --verbose, -v (no value)
         | FlagCount    // -v -v -v (stackable counter)
         | FlagValue    // --output FILE (takes a value)

// A single argument specification.
ArgSpec: {
    long_name:   Str,
    short_name:  CChar,     // '\0' for none
    description: Str,
    kind:        ArgKind,
    required:    Int,       // 1 = required, 0 = optional
}

// Parsed value for a single argument.
ArgValue: | BoolVal  { v: Bool }
          | CountVal { v: Int }
          | StrVal   { v: Str }
          | Absent

// Individual parse error.
ParseError: | UnknownFlag     { flag: Str }
            | MissingValue    { flag: Str }
            | MissingRequired { name: Str }
            | InvalidCommand  { name: Str }

// Collection of all errors from a parse.
ParseErrors: { errors: Array[ParseError] }

// Parsed arguments container.
Args: {
    _values:     Ptr[HashMap[Str, ArgValue]],
    _positional: Array[Str],
    _rest:       Array[Str],
    _command:    Option[Str],
}

// Parser specification (builder).
Parser: {
    _alloc:       Ptr[Allocator],
    _name:        Str,
    _description: Str,
    _specs:       Array[ArgSpec],
    _commands:    Array[Str],
}

// -- Synopsis --

create (alloc: Ptr[Allocator], name: Str, description: Str) -> Ptr[Parser]
create (name: Str, description: Str)                         -> Ptr[Parser]
destroy(self: Ptr[Parser])                                   -> Void

flag    (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
flag    (self: Ptr[Parser], long_name: Str, desc: Str)                    -> Void
counter (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
option  (self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
option  (self: Ptr[Parser], long_name: Str, desc: Str)                    -> Void
required(self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void
required(self: Ptr[Parser], long_name: Str, desc: Str)                    -> Void
command (self: Ptr[Parser], name: Str)                                    -> Void

parse        (self: Ptr[Parser], argc: Int, argv: Ptr[CString]) -> Result[Args, ParseErrors]
parse_or_exit(self: Ptr[Parser], argc: Int, argv: Ptr[CString]) -> Args

get_flag   (args: Args, name: Str) -> Bool
get_count  (args: Args, name: Str) -> Int
get_option (args: Args, name: Str) -> Option[Str]
get_command(args: Args)            -> Option[Str]
positionals(args: Args)            -> Array[Str]
rest       (args: Args)            -> Array[Str]
has        (args: Args, name: Str) -> Bool

help_text (self: Ptr[Parser]) -> Str
print_help(self: Ptr[Parser]) -> Void

from_argv(argc: Int, argv: Ptr[CString]) -> Array[Str]

// -- Private helpers --

_find_spec_long (self: Ptr[Parser], name: Str)       -> Option[CSize]
_find_spec_short(self: Ptr[Parser], ch: CChar)        -> Option[CSize]
_store_bool     (values: Ptr[HashMap[Str, ArgValue]], name: Str) -> Void
_store_count    (values: Ptr[HashMap[Str, ArgValue]], name: Str) -> Void
_store_str      (values: Ptr[HashMap[Str, ArgValue]], name: Str, val: Str) -> Void


// -- Implementation --


// -- Create / Destroy --

create(alloc: Ptr[Allocator], name: Str, description: Str) -> Ptr[Parser] {
    p: Ptr[Parser] := alloc->malloc(alloc, sizeof[Parser]())
    if p == null { _tl_fatal_("Cmdline: oom") }
    p->_alloc       = alloc
    p->_name        = name
    p->_description = description
    p->_specs       = Array.empty[ArgSpec]()
    p->_commands    = Array.empty[Str]()
    p
}

create(name: Str, description: Str) -> Ptr[Parser] {
    create(Alloc.context.default, name, description)
}

destroy(self: Ptr[Parser]) -> Void {
    alloc := self->_alloc
    Array.free(self->_specs.&)
    Array.free(self->_commands.&)
    alloc->free(alloc, self)
}


// -- Spec builders --

flag(self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagBool, required = 0)
    Array.push(self->_specs.&, spec)
}

flag(self: Ptr[Parser], long_name: Str, desc: Str) -> Void {
    flag(self, long_name, '\0', desc)
}

counter(self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagCount, required = 0)
    Array.push(self->_specs.&, spec)
}

option(self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = 0)
    Array.push(self->_specs.&, spec)
}

option(self: Ptr[Parser], long_name: Str, desc: Str) -> Void {
    option(self, long_name, '\0', desc)
}

required(self: Ptr[Parser], long_name: Str, short_name: CChar, desc: Str) -> Void {
    spec := ArgSpec(long_name = long_name, short_name = short_name,
                    description = desc, kind = FlagValue, required = 1)
    Array.push(self->_specs.&, spec)
}

required(self: Ptr[Parser], long_name: Str, desc: Str) -> Void {
    required(self, long_name, '\0', desc)
}

command(self: Ptr[Parser], name: Str) -> Void {
    Array.push(self->_commands.&, name)
}


// -- Private helpers --

_find_spec_long(self: Ptr[Parser], name: Str) -> Option[CSize] {
    i: CSize := 0zu
    while i < self->_specs.size, i += 1zu {
        if Str.eq(self->_specs.v.[i].long_name, name) {
            return Some(i)
        }
    }
    None
}

_find_spec_short(self: Ptr[Parser], ch: CChar) -> Option[CSize] {
    if ch == '\0' { return None }
    i: CSize := 0zu
    while i < self->_specs.size, i += 1zu {
        if self->_specs.v.[i].short_name == ch {
            return Some(i)
        }
    }
    None
}

_store_bool(values: Ptr[HashMap[Str, ArgValue]], name: Str) -> Void {
    HashMap.set(values, name, BoolVal(v = true))
}

_store_count(values: Ptr[HashMap[Str, ArgValue]], name: Str) -> Void {
    s: Some := HashMap.get_copy(values, name) else {
        HashMap.set(values, name, CountVal(v = 1))
        return
    }
    when s.v {
        c: CountVal { HashMap.set(values, name, CountVal(v = c.v + 1)) }
        else        { HashMap.set(values, name, CountVal(v = 1))       }
    }
}

_store_str(values: Ptr[HashMap[Str, ArgValue]], name: Str, val: Str) -> Void {
    HashMap.set(values, name, StrVal(v = val))
}

_parse_long(self: Ptr[Parser], values: Ptr[HashMap[Str, ArgValue]],
            errors: Ptr[Array[ParseError]], name: Str, arg: Str,
            argc: Int, argv: Ptr[CString], ip: Ptr[Int]) -> Void {
    when Str.index_of_char(name, '=') {
        eq_pos: Some {
            key := Str.slice(name, 0zu, eq_pos.v)
            val := Str.slice(name, eq_pos.v + 1zu, Str.len(name))
            si: Some := _find_spec_long(self, key) else {
                Array.push(errors, UnknownFlag(flag = arg))
                return
            }
            spec := self->_specs.v.[si.v]
            when spec.kind {
                fv: FlagValue { _store_str(values, key, val) }
                else          { Array.push(errors, UnknownFlag(flag = arg)) }
            }
        }
        no_eq: None {
            si: Some := _find_spec_long(self, name) else {
                Array.push(errors, UnknownFlag(flag = arg))
                return
            }
            spec := self->_specs.v.[si.v]
            when spec.kind {
                fb: FlagBool  { _store_bool(values, name)  }
                fc: FlagCount { _store_count(values, name) }
                fv: FlagValue {
                    if ip.* + 1 < argc {
                        ip.* = ip.* + 1
                        _store_str(values, name, Str.from_cstr(argv.[ip.*]))
                    } else {
                        Array.push(errors, MissingValue(flag = name))
                    }
                }
            }
        }
    }
}

_parse_short(self: Ptr[Parser], values: Ptr[HashMap[Str, ArgValue]],
             errors: Ptr[Array[ParseError]], arg: Str,
             argc: Int, argv: Ptr[CString], ip: Ptr[Int]) -> Void {
    arg_len := Str.len(arg)
    j: CSize := 1zu
    while j < arg_len {
        bv: Some := Str.byte_at(arg, j) else {
            j += 1zu
            continue
        }
        ch: CChar := bv.v
        si: Some := _find_spec_short(self, ch) else {
            flag_str := Str.cat(_S("-"), Str.from_bytes(ch.&, 1zu))
            Array.push(errors, UnknownFlag(flag = flag_str))
            j += 1zu
            continue
        }
        spec := self->_specs.v.[si.v]
        when spec.kind {
            fb: FlagBool  { _store_bool(values, spec.long_name)  }
            fc: FlagCount { _store_count(values, spec.long_name) }
            fv: FlagValue {
                if j + 1zu < arg_len {
                    val := Str.slice(arg, j + 1zu, arg_len)
                    _store_str(values, spec.long_name, val)
                    j = arg_len
                } else if ip.* + 1 < argc {
                    ip.* = ip.* + 1
                    _store_str(values, spec.long_name, Str.from_cstr(argv.[ip.*]))
                } else {
                    Array.push(errors, MissingValue(flag = spec.long_name))
                }
            }
        }
        j += 1zu
    }
}


// -- Parse --

parse(self: Ptr[Parser], argc: Int, argv: Ptr[CString]) -> Result[Args, ParseErrors] {
    values := HashMap.create[Str, ArgValue]()
    positional := Array.empty[Str]()
    rest_args  := Array.empty[Str]()
    errors     := Array.empty[ParseError]()
    cmd: Option[Str] := None

    dd           := _S("--")
    short_prefix := _S("-")

    i := 1; past_separator := 0
    while i < argc {
        arg := Str.from_cstr(argv.[i])

        // After --, everything is rest
        if past_separator != 0 {
            Array.push(rest_args.&, arg)
            i += 1
            continue
        }

        // Check for --
        if Str.eq(arg, dd) {
            past_separator = 1
            i += 1
            continue
        }

        // Check for --long options
        if Str.starts_with(arg, dd) {
            name := Str.slice(arg, 2zu, Str.len(arg))
            _parse_long(self, values, errors.&, name, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Check for -x (short options)
        if Str.starts_with(arg, short_prefix) && Str.len(arg) > 1zu {
            _parse_short(self, values, errors.&, arg, argc, argv, i.&)
            i += 1
            continue
        }

        // Not a flag — check for command or positional
        if self->_commands.size > 0zu && positional.size == 0zu {
            found_cmd := 0
            k: CSize := 0zu
            while k < self->_commands.size, k += 1zu {
                if Str.eq(self->_commands.v.[k], arg) {
                    cmd = Some(arg)
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
    r: CSize := 0zu
    while r < self->_specs.size, r += 1zu {
        spec := self->_specs.v.[r]
        if spec.required != 0 {
            if !HashMap.contains(values, spec.long_name) {
                Array.push(errors.&, MissingRequired(name = spec.long_name))
            }
        }
    }

    args := Args(_values = values, _positional = positional, _rest = rest_args, _command = cmd)

    if errors.size > 0zu {
        Err(ParseErrors(errors = errors))
    } else {
        Ok(args)
    }
}

parse_or_exit(self: Ptr[Parser], argc: Int, argv: Ptr[CString]) -> Args {
    result := parse(self, argc, argv)
    when result {
        ok: Ok { ok.v }
        err: Err {
            i: CSize := 0zu
            while i < err.v.errors.size, i += 1zu {
                when err.v.errors.v.[i] {
                    uf: UnknownFlag     { c_printf("error: unknown flag '%s'\n", Str.cstr(uf.flag.&))     }
                    mv: MissingValue    { c_printf("error: missing value for '%s'\n", Str.cstr(mv.flag.&)) }
                    mr: MissingRequired { c_printf("error: missing required option '%s'\n", Str.cstr(mr.name.&)) }
                    ic: InvalidCommand  { c_printf("error: unknown command '%s'\n", Str.cstr(ic.name.&))   }
                }
            }
            c_printf("\n")
            print_help(self)
            c_exit(1)
            _tl_fatal_("unreachable")
        }
    }
}


// -- Query --

get_flag(args: Args, name: Str) -> Bool {
    s: Some := HashMap.get_copy(args._values, name) else { return false }
    when s.v {
        b: BoolVal { b.v }
        else       { false }
    }
}

get_count(args: Args, name: Str) -> Int {
    s: Some := HashMap.get_copy(args._values, name) else { return 0 }
    when s.v {
        c: CountVal { c.v }
        else        { 0   }
    }
}

get_option(args: Args, name: Str) -> Option[Str] {
    s: Some := HashMap.get_copy(args._values, name) else { return None }
    when s.v {
        sv: StrVal { Some(sv.v) }
        else       { None       }
    }
}

get_command(args: Args) -> Option[Str] { args._command }

positionals(args: Args) -> Array[Str] { args._positional }

rest(args: Args) -> Array[Str] { args._rest }

has(args: Args, name: Str) -> Bool {
    HashMap.contains(args._values, name)
}


// -- Help --

help_text(self: Ptr[Parser]) -> Str {
    result := Str.cat(_S("Usage: "), self->_name)
    result = Str.cat(result, _S(" [options]"))

    if self->_commands.size > 0zu {
        result = Str.cat(result, _S(" <command>"))
    }

    result = Str.cat(result, _S(" [args...]\n"))

    if Str.len(self->_description) > 0zu {
        result = Str.cat(result, _S("\n"))
        result = Str.cat(result, self->_description)
        result = Str.cat(result, _S("\n"))
    }

    if self->_specs.size > 0zu {
        result = Str.cat(result, _S("\nOptions:\n"))

        i: CSize := 0zu
        while i < self->_specs.size, i += 1zu {
            spec := self->_specs.v.[i]
            result = Str.cat(result, _S("  "))

            if spec.short_name != '\0' {
                result = Str.cat(result, _S("-"))
                result = Str.cat(result, Str.from_bytes(spec.short_name.&, 1zu))
                result = Str.cat(result, _S(", "))
            } else {
                result = Str.cat(result, _S("    "))
            }

            result = Str.cat(result, _S("--"))
            result = Str.cat(result, spec.long_name)

            when spec.kind {
                fv: FlagValue { result = Str.cat(result, _S(" <value>")) }
                else          { }
            }

            result = Str.cat(result, _S("\t"))
            result = Str.cat(result, spec.description)

            if spec.required != 0 {
                result = Str.cat(result, _S(" (required)"))
            }

            result = Str.cat(result, _S("\n"))
        }
    }

    if self->_commands.size > 0zu {
        result = Str.cat(result, _S("\nCommands:\n"))
        j: CSize := 0zu
        while j < self->_commands.size, j += 1zu {
            result = Str.cat(result, _S("  "))
            result = Str.cat(result, self->_commands.v.[j])
            result = Str.cat(result, _S("\n"))
        }
    }

    result
}

print_help(self: Ptr[Parser]) -> Void {
    txt := help_text(self)
    c_printf("%s", Str.cstr(txt.&))
}


// -- Utility --

from_argv(argc: Int, argv: Ptr[CString]) -> Array[Str] {
    result := Array.empty[Str]()
    i := 0
    while i < argc, i += 1 {
        Array.push(result.&, Str.from_cstr(argv.[i]))
    }
    result
}
