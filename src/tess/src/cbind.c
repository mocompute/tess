#include "cbind.h"

#include "file.h"
#include "hashmap.h"
#include "platform.h"
#include "str.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------
// C token types
// ---------------------------------------------------------------------------

typedef enum {
    CTK_EOF,
    CTK_IDENT,
    CTK_NUMBER,
    CTK_STRING,
    CTK_CHAR_LIT,
    CTK_LPAREN,
    CTK_RPAREN,
    CTK_LBRACE,
    CTK_RBRACE,
    CTK_LBRACKET,
    CTK_RBRACKET,
    CTK_SEMICOLON,
    CTK_COMMA,
    CTK_STAR,
    CTK_EQUALS,
    CTK_ELLIPSIS,
    CTK_HASH,
    CTK_NEWLINE,
    CTK_COLON,
    CTK_OTHER,
} c_token_kind;

typedef struct {
    c_token_kind kind;
    char const  *start;
    u32          len;
} c_token;

// ---------------------------------------------------------------------------
// Parsed C type representation
// ---------------------------------------------------------------------------

typedef enum {
    CT_VOID,
    CT_CHAR,
    CT_SCHAR,
    CT_UCHAR,
    CT_SHORT,
    CT_USHORT,
    CT_INT,
    CT_UINT,
    CT_LONG,
    CT_ULONG,
    CT_LONGLONG,
    CT_ULONGLONG,
    CT_FLOAT,
    CT_DOUBLE,
    CT_LONGDOUBLE,
    CT_BOOL,
    CT_SIZE_T,
    CT_SSIZE_T,
    CT_PTRDIFF_T,
    CT_INT8,
    CT_UINT8,
    CT_INT16,
    CT_UINT16,
    CT_INT32,
    CT_UINT32,
    CT_INT64,
    CT_UINT64,
    CT_INTPTR,
    CT_UINTPTR,
    CT_NAMED,
    CT_FUNC_PTR,
    CT_UNKNOWN,
} c_type_kind;

typedef struct c_type  c_type;
typedef struct c_param c_param;

struct c_type {
    c_type_kind kind;
    str         name; // for CT_NAMED
    int         is_const;
    int         pointer_depth;
    int         const_at[4]; // is_const at each pointer level

    // function pointer
    c_param *fp_params;
    u32      fp_param_count;
    c_type  *fp_ret;
    int      fp_variadic;

    u32      array_size; // 0 = not an array, >0 = fixed-size array
};

struct c_param {
    c_type type;
    str    name;
};

// ---------------------------------------------------------------------------
// Parsed declarations
// ---------------------------------------------------------------------------

typedef enum {
    CDECL_FUNCTION,
    CDECL_STRUCT,
    CDECL_TYPEDEF,
    CDECL_ENUM_VALUE,
    CDECL_DEFINE,
    CDECL_FORWARD_STRUCT,
} c_decl_kind;

typedef struct {
    str    name;
    c_type type;
} c_field;

typedef struct {
    c_decl_kind kind;
    str         name;

    // CDECL_FUNCTION
    c_type   return_type;
    c_param *params;
    u32      param_count;
    int      is_variadic;

    // CDECL_STRUCT
    c_field *fields;
    u32      field_count;

    // CDECL_ENUM_VALUE (no extra data needed beyond name)

    // CDECL_TYPEDEF
    c_type aliased_type;
    str    struct_tag; // for "typedef struct tag ... name;"
} c_decl;

defarray(decl_array, c_decl);
defarray(field_array, c_field);
defarray(param_array, c_param);

// ---------------------------------------------------------------------------
// Parser state
// ---------------------------------------------------------------------------

typedef struct {
    allocator  *alloc;
    char const *src;
    u32         src_len;
    u32         pos;

    str         target_file;
    char const *target_basename; // cached for fast comparison
    int         in_target;

    decl_array  decls;

    hashmap    *typedefs;      // str -> str (C name -> tess type string)
    hashmap    *struct_defs;   // str -> int (struct tag -> 1 if body seen)
    hashmap    *forward_decls; // str hashset (struct tags with forward decl emitted)

    int         verbose;
} cbind_state;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

static c_token next_token(cbind_state *st);
static c_token peek_token(cbind_state *st);
static void    skip_whitespace(cbind_state *st);
static str     tok_str(allocator *a, c_token t);
static void    parse_all(cbind_state *st);
static int     parse_define(cbind_state *st);
static int     parse_toplevel_decl(cbind_state *st);
static c_type  parse_type(cbind_state *st);
static void    skip_to_semicolon(cbind_state *st);
static void    skip_to_end_of_line(cbind_state *st);
static void    skip_braced_block(cbind_state *st);
static str     type_to_tess(allocator *a, c_type const *t, hashmap *typedefs);
static str     emit_bindings(allocator *a, cbind_state *st, char const *module_name);

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

static void skip_whitespace(cbind_state *st) {
    while (st->pos < st->src_len) {
        char c = st->src[st->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            st->pos++;
        } else {
            break;
        }
    }
}

static c_token make_token(c_token_kind kind, char const *start, u32 len) {
    return (c_token){.kind = kind, .start = start, .len = len};
}

// next_token_raw: returns NEWLINE tokens (used for line-sensitive parsing)
static c_token next_token_raw(cbind_state *st) {
    skip_whitespace(st);
    if (st->pos >= st->src_len) return make_token(CTK_EOF, st->src + st->src_len, 0);

    char const *start = st->src + st->pos;
    char        c     = *start;

    // newline
    if (c == '\n') {
        st->pos++;
        return make_token(CTK_NEWLINE, start, 1);
    }

    // single-char tokens
    switch (c) {
    case '(': st->pos++; return make_token(CTK_LPAREN, start, 1);
    case ')': st->pos++; return make_token(CTK_RPAREN, start, 1);
    case '{': st->pos++; return make_token(CTK_LBRACE, start, 1);
    case '}': st->pos++; return make_token(CTK_RBRACE, start, 1);
    case '[': st->pos++; return make_token(CTK_LBRACKET, start, 1);
    case ']': st->pos++; return make_token(CTK_RBRACKET, start, 1);
    case ';': st->pos++; return make_token(CTK_SEMICOLON, start, 1);
    case ',': st->pos++; return make_token(CTK_COMMA, start, 1);
    case '*': st->pos++; return make_token(CTK_STAR, start, 1);
    case '=': st->pos++; return make_token(CTK_EQUALS, start, 1);
    case '#': st->pos++; return make_token(CTK_HASH, start, 1);
    case ':': st->pos++; return make_token(CTK_COLON, start, 1);
    default:  break;
    }

    // ellipsis
    if (c == '.' && st->pos + 2 < st->src_len && st->src[st->pos + 1] == '.' &&
        st->src[st->pos + 2] == '.') {
        st->pos += 3;
        return make_token(CTK_ELLIPSIS, start, 3);
    }

    // string literal
    if (c == '"') {
        st->pos++;
        while (st->pos < st->src_len && st->src[st->pos] != '"') {
            if (st->src[st->pos] == '\\') st->pos++;
            st->pos++;
        }
        if (st->pos < st->src_len) st->pos++; // closing quote
        return make_token(CTK_STRING, start, (u32)(st->src + st->pos - start));
    }

    // char literal
    if (c == '\'') {
        st->pos++;
        while (st->pos < st->src_len && st->src[st->pos] != '\'') {
            if (st->src[st->pos] == '\\') st->pos++;
            st->pos++;
        }
        if (st->pos < st->src_len) st->pos++;
        return make_token(CTK_CHAR_LIT, start, (u32)(st->src + st->pos - start));
    }

    // number (don't start with '-'; that's a separate operator token)
    if (isdigit((unsigned char)c)) {
        st->pos++;
        char prev = c;
        while (st->pos < st->src_len) {
            char ch = st->src[st->pos];
            if (isalnum((unsigned char)ch) || ch == '.' || ch == '_') {
                prev = ch;
                st->pos++;
            } else if ((ch == '+' || ch == '-') &&
                       (prev == 'e' || prev == 'E' || prev == 'p' || prev == 'P')) {
                // exponent sign: 1e+5, 0x1.0p-3
                prev = ch;
                st->pos++;
            } else {
                break;
            }
        }
        return make_token(CTK_NUMBER, start, (u32)(st->src + st->pos - start));
    }

    // identifier
    if (isalpha((unsigned char)c) || c == '_') {
        st->pos++;
        while (st->pos < st->src_len &&
               (isalnum((unsigned char)st->src[st->pos]) || st->src[st->pos] == '_')) {
            st->pos++;
        }
        return make_token(CTK_IDENT, start, (u32)(st->src + st->pos - start));
    }

    // anything else
    st->pos++;
    return make_token(CTK_OTHER, start, 1);
}

// next_token: skips newlines and auto-processes # line markers.
// This makes line markers invisible to all parser code — they can appear
// anywhere in multi-line declarations (between ')' and ';', between struct
// fields, etc.) and the parser doesn't need to know.
// Only #define directives are returned as CTK_HASH tokens.
static c_token next_token(cbind_state *st) {
    for (;;) {
        c_token t = next_token_raw(st);
        if (t.kind == CTK_NEWLINE) continue;
        if (t.kind == CTK_HASH) {
            u32     saved = st->pos;
            c_token next  = next_token_raw(st);
            // skip intervening newlines between # and the number/directive
            while (next.kind == CTK_NEWLINE) next = next_token_raw(st);
            if (next.kind == CTK_NUMBER) {
                // line marker: # N "file" [flags] — process and skip
                c_token file = next_token_raw(st);
                while (file.kind == CTK_NEWLINE) file = next_token_raw(st);
                if (file.kind == CTK_STRING && file.len >= 2) {
                    // find basename directly in source buffer (no allocation)
                    char const *path = file.start + 1;
                    u32         plen = file.len - 2;
                    char const *base = path + plen;
                    while (base > path && base[-1] != '/' && base[-1] != '\\') base--;
                    u32 blen      = (u32)(path + plen - base);
                    u32 tlen      = (u32)strlen(st->target_basename);
                    st->in_target = (blen == tlen && 0 == memcmp(base, st->target_basename, tlen));
                }
                skip_to_end_of_line(st);
                continue;
            }
            // not a line marker (e.g. #define) — restore pos, return hash
            st->pos = saved;
        }
        return t;
    }
}

static c_token peek_token(cbind_state *st) {
    u32     saved_pos    = st->pos;
    int     saved_target = st->in_target;
    c_token t            = next_token(st);
    st->pos              = saved_pos;
    st->in_target        = saved_target;
    return t;
}

// Use sizeof to get compile-time string length instead of calling strlen
#define tok_eq(t, s) ((t).len == (sizeof(s) - 1) && 0 == memcmp((t).start, s, sizeof(s) - 1))

static str tok_str(allocator *a, c_token t) {
    return str_init_n(a, t.start, t.len);
}

// ---------------------------------------------------------------------------
// Skip helpers
// ---------------------------------------------------------------------------

static void skip_to_end_of_line(cbind_state *st) {
    while (st->pos < st->src_len && st->src[st->pos] != '\n') {
        st->pos++;
    }
}

static void skip_to_semicolon(cbind_state *st) {
    int depth = 0;
    while (st->pos < st->src_len) {
        c_token t = next_token(st);
        if (t.kind == CTK_EOF) break;
        if (t.kind == CTK_LBRACE) depth++;
        else if (t.kind == CTK_RBRACE) {
            depth--;
            if (depth <= 0) {
                // consume trailing semicolon if present
                c_token p = peek_token(st);
                if (p.kind == CTK_SEMICOLON) next_token(st);
                return;
            }
        } else if (t.kind == CTK_SEMICOLON && depth == 0) {
            return;
        }
    }
}

static void skip_braced_block(cbind_state *st) {
    int depth = 1;
    while (st->pos < st->src_len) {
        c_token t = next_token(st);
        if (t.kind == CTK_EOF) break;
        if (t.kind == CTK_LBRACE) depth++;
        else if (t.kind == CTK_RBRACE) {
            depth--;
            if (depth == 0) return;
        }
    }
}

// ---------------------------------------------------------------------------
// Skip GCC/Clang attributes
// ---------------------------------------------------------------------------

static void skip_paren_block(cbind_state *st) {
    c_token p = peek_token(st);
    if (p.kind != CTK_LPAREN) return;
    next_token(st);
    int depth = 1;
    while (depth > 0 && st->pos < st->src_len) {
        c_token x = next_token(st);
        if (x.kind == CTK_LPAREN) depth++;
        else if (x.kind == CTK_RPAREN) depth--;
        else if (x.kind == CTK_EOF) break;
    }
}

static int skip_attribute(cbind_state *st) {
    c_token t = peek_token(st);
    if (t.kind != CTK_IDENT) return 0;

    // Compiler __ keywords with mandatory or optional paren block
    if (tok_eq(t, "__attribute__") || tok_eq(t, "__attribute") || tok_eq(t, "__declspec") ||
        tok_eq(t, "__typeof__") || tok_eq(t, "__typeof") || tok_eq(t, "__asm__") || tok_eq(t, "__asm") ||
        tok_eq(t, "__nonnull")) {
        next_token(st);
        skip_paren_block(st);
        return 1;
    }

    // Compiler __ keywords (bare, no parens)
    if (tok_eq(t, "__extension__") || tok_eq(t, "__restrict") || tok_eq(t, "__restrict__") ||
        tok_eq(t, "__inline") || tok_eq(t, "__inline__") || tok_eq(t, "__volatile__") ||
        tok_eq(t, "__volatile") || tok_eq(t, "__signed__") || tok_eq(t, "__signed") ||
        tok_eq(t, "__const") || tok_eq(t, "__const__") || tok_eq(t, "__auto_type")) {
        next_token(st);
        return 1;
    }

    // C keywords and single-underscore attributes
    if (tok_eq(t, "_Alignas") || tok_eq(t, "_Alignof") || tok_eq(t, "_Pragma")) {
        next_token(st);
        skip_paren_block(st);
        return 1;
    }
    if (tok_eq(t, "typeof")) {
        next_token(st);
        skip_paren_block(st);
        return 1;
    }
    if (tok_eq(t, "_Atomic")) {
        next_token(st);
        skip_paren_block(st);
        return 1;
    }
    if (tok_eq(t, "restrict") || tok_eq(t, "inline") || tok_eq(t, "_Noreturn") || tok_eq(t, "_Complex") ||
        tok_eq(t, "_Imaginary")) {
        next_token(st);
        return 1;
    }

    return 0;
}

static void skip_all_attributes(cbind_state *st) {
    while (skip_attribute(st)) {
        // keep going
    }
}

// ---------------------------------------------------------------------------
// Type parser
// ---------------------------------------------------------------------------

static c_type parse_type(cbind_state *st) {
    c_type t = {0};

    skip_all_attributes(st);

    // collect type specifiers
    int saw_unsigned = 0;
    int saw_signed   = 0;
    int saw_long     = 0;
    int saw_short    = 0;
    int saw_int      = 0;
    int saw_char     = 0;
    int saw_float    = 0;
    int saw_double   = 0;
    int saw_void     = 0;
    int saw_const    = 0;
    int saw_struct   = 0;
    int saw_enum     = 0;
    int saw_union    = 0;
    int saw_named    = 0;
    str named        = str_empty();

    for (;;) {

        skip_all_attributes(st);
        c_token p = peek_token(st);
        if (p.kind != CTK_IDENT) break;

        if (tok_eq(p, "const") || tok_eq(p, "__const") || tok_eq(p, "__const__")) {
            next_token(st);
            saw_const = 1;
            continue;
        }
        if (tok_eq(p, "volatile") || tok_eq(p, "__volatile") || tok_eq(p, "__volatile__")) {
            next_token(st);
            continue;
        }
        if (tok_eq(p, "unsigned") || tok_eq(p, "__unsigned__")) {
            next_token(st);
            saw_unsigned = 1;
            continue;
        }
        if (tok_eq(p, "signed") || tok_eq(p, "__signed__") || tok_eq(p, "__signed")) {
            next_token(st);
            saw_signed = 1;
            continue;
        }
        if (tok_eq(p, "long")) {
            next_token(st);
            saw_long++;
            continue;
        }
        if (tok_eq(p, "short")) {
            next_token(st);
            saw_short = 1;
            continue;
        }
        if (tok_eq(p, "int")) {
            next_token(st);
            saw_int = 1;
            continue;
        }
        if (tok_eq(p, "char")) {
            next_token(st);
            saw_char = 1;
            continue;
        }
        if (tok_eq(p, "float")) {
            next_token(st);
            saw_float = 1;
            continue;
        }
        if (tok_eq(p, "double")) {
            next_token(st);
            saw_double = 1;
            continue;
        }
        if (tok_eq(p, "void")) {
            next_token(st);
            saw_void = 1;
            continue;
        }
        if (tok_eq(p, "struct")) {
            next_token(st);
            saw_struct       = 1;
            c_token name_tok = peek_token(st);
            if (name_tok.kind == CTK_IDENT) {
                next_token(st);
                named     = tok_str(st->alloc, name_tok);
                saw_named = 1;
            }
            continue;
        }
        if (tok_eq(p, "enum")) {
            next_token(st);
            saw_enum         = 1;
            c_token name_tok = peek_token(st);
            if (name_tok.kind == CTK_IDENT) {
                next_token(st);
                named     = tok_str(st->alloc, name_tok);
                saw_named = 1;
            }
            continue;
        }
        if (tok_eq(p, "union")) {
            next_token(st);
            saw_union        = 1;
            c_token name_tok = peek_token(st);
            if (name_tok.kind == CTK_IDENT) {
                next_token(st);
                named     = tok_str(st->alloc, name_tok);
                saw_named = 1;
            }
            continue;
        }

        // known typedefs for standard types
        if (tok_eq(p, "size_t") || tok_eq(p, "__size_t")) {
            next_token(st);
            t.kind    = CT_SIZE_T;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "ssize_t") || tok_eq(p, "__ssize_t")) {
            next_token(st);
            t.kind    = CT_SSIZE_T;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "ptrdiff_t") || tok_eq(p, "__ptrdiff_t")) {
            next_token(st);
            t.kind    = CT_PTRDIFF_T;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "int8_t") || tok_eq(p, "__int8_t")) {
            next_token(st);
            t.kind    = CT_INT8;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "uint8_t") || tok_eq(p, "__uint8_t")) {
            next_token(st);
            t.kind    = CT_UINT8;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "int16_t") || tok_eq(p, "__int16_t")) {
            next_token(st);
            t.kind    = CT_INT16;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "uint16_t") || tok_eq(p, "__uint16_t")) {
            next_token(st);
            t.kind    = CT_UINT16;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "int32_t") || tok_eq(p, "__int32_t")) {
            next_token(st);
            t.kind    = CT_INT32;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "uint32_t") || tok_eq(p, "__uint32_t")) {
            next_token(st);
            t.kind    = CT_UINT32;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "int64_t") || tok_eq(p, "__int64_t")) {
            next_token(st);
            t.kind    = CT_INT64;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "uint64_t") || tok_eq(p, "__uint64_t")) {
            next_token(st);
            t.kind    = CT_UINT64;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "intptr_t") || tok_eq(p, "__intptr_t")) {
            next_token(st);
            t.kind    = CT_INTPTR;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "uintptr_t") || tok_eq(p, "__uintptr_t")) {
            next_token(st);
            t.kind    = CT_UINTPTR;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "bool") || tok_eq(p, "_Bool")) {
            next_token(st);
            t.kind    = CT_BOOL;
            saw_named = 1;
            continue;
        }
        if (tok_eq(p, "__builtin_va_list")) {
            next_token(st);
            named     = str_init(st->alloc, "__builtin_va_list");
            saw_named = 1;
            break;
        }

        // if we already collected specifiers, this must be the declarator name — stop
        if (saw_int || saw_char || saw_float || saw_double || saw_void || saw_unsigned || saw_signed ||
            saw_long || saw_short || saw_named) {
            break;
        }

        // must be a typedef name or unknown type
        next_token(st);
        named     = tok_str(st->alloc, p);
        saw_named = 1;
        break;
    }

    // resolve the specifiers into a type kind
    if (t.kind == CT_BOOL || t.kind == CT_SIZE_T || t.kind == CT_SSIZE_T || t.kind == CT_PTRDIFF_T ||
        t.kind == CT_INT8 || t.kind == CT_UINT8 || t.kind == CT_INT16 || t.kind == CT_UINT16 ||
        t.kind == CT_INT32 || t.kind == CT_UINT32 || t.kind == CT_INT64 || t.kind == CT_UINT64 ||
        t.kind == CT_INTPTR || t.kind == CT_UINTPTR) {
        // already set
    } else if (saw_void) {
        t.kind = CT_VOID;
    } else if (saw_float) {
        t.kind = CT_FLOAT;
    } else if (saw_double && saw_long) {
        t.kind = CT_LONGDOUBLE;
    } else if (saw_double) {
        t.kind = CT_DOUBLE;
    } else if (saw_char) {
        if (saw_unsigned) t.kind = CT_UCHAR;
        else if (saw_signed) t.kind = CT_SCHAR;
        else t.kind = CT_CHAR;
    } else if (saw_short) {
        t.kind = saw_unsigned ? CT_USHORT : CT_SHORT;
    } else if (saw_long >= 2) {
        t.kind = saw_unsigned ? CT_ULONGLONG : CT_LONGLONG;
    } else if (saw_long == 1) {
        t.kind = saw_unsigned ? CT_ULONG : CT_LONG;
    } else if (saw_unsigned) {
        t.kind = CT_UINT;
    } else if (saw_signed || saw_int) {
        t.kind = CT_INT;
    } else if (saw_struct && !str_is_empty(named)) {
        t.kind = CT_NAMED;
        t.name = str_fmt(st->alloc, "struct %.*s", str_ilen(named), str_buf(&named));
    } else if (saw_enum && !str_is_empty(named)) {
        t.kind = CT_INT; // enums are ints in Tess bindings
    } else if (saw_union && !str_is_empty(named)) {
        t.kind = CT_NAMED;
        t.name = str_fmt(st->alloc, "union %.*s", str_ilen(named), str_buf(&named));
    } else if (!str_is_empty(named)) {
        t.kind = CT_NAMED;
        t.name = named;
    } else {
        t.kind = CT_UNKNOWN;
    }

    t.is_const = saw_const;

    // parse pointer chain
    for (;;) {

        skip_all_attributes(st);
        c_token p = peek_token(st);
        if (p.kind == CTK_STAR) {
            next_token(st);
            if (t.pointer_depth < 4) {
                t.pointer_depth++;
            }
            // check for const after *
            skip_all_attributes(st);
            p = peek_token(st);
            if (p.kind == CTK_IDENT &&
                (tok_eq(p, "const") || tok_eq(p, "__const") || tok_eq(p, "__const__"))) {
                next_token(st);
                if (t.pointer_depth > 0 && t.pointer_depth <= 4) {
                    t.const_at[t.pointer_depth - 1] = 1;
                }
            }
            // skip restrict after pointer
            p = peek_token(st);
            if (p.kind == CTK_IDENT &&
                (tok_eq(p, "restrict") || tok_eq(p, "__restrict") || tok_eq(p, "__restrict__"))) {
                next_token(st);
            }
        } else {
            break;
        }
    }

    return t;
}

// ---------------------------------------------------------------------------
// Parse function pointer parameter: type (*name)(params)
// Also handles regular params: type name
// ---------------------------------------------------------------------------

static c_param parse_param(cbind_state *st) {

    c_type  type = parse_type(st);
    c_param p    = {.type = type, .name = str_empty()};

    skip_all_attributes(st);
    c_token tok = peek_token(st);

    // function pointer: (*name)(params)
    if (tok.kind == CTK_LPAREN) {
        u32 saved = st->pos;
        next_token(st); // consume (
        c_token star_or_caret = peek_token(st);
        if (star_or_caret.kind == CTK_STAR) {
            next_token(st); // consume *
            c_token name_tok = peek_token(st);
            if (name_tok.kind == CTK_IDENT) {
                p.name = tok_str(st->alloc, name_tok);
                next_token(st);
            }
            c_token rparen = peek_token(st);
            if (rparen.kind == CTK_RPAREN) {
                next_token(st); // consume )
                // now parse parameter list
                c_token lparen = peek_token(st);
                if (lparen.kind == CTK_LPAREN) {
                    next_token(st); // consume (
                    // build function pointer type
                    c_type fp         = {0};
                    fp.kind           = CT_FUNC_PTR;
                    fp.fp_ret         = alloc_malloc(st->alloc, sizeof(c_type));
                    *fp.fp_ret        = type;
                    param_array fps   = {.alloc = st->alloc};

                    c_token     check = peek_token(st);
                    if (check.kind == CTK_RPAREN) {
                        next_token(st); // empty params
                    } else if (check.kind == CTK_IDENT && tok_eq(check, "void")) {
                        u32     saved2 = st->pos;
                        c_token v      = next_token(st);
                        (void)v;
                        c_token after = peek_token(st);
                        if (after.kind == CTK_RPAREN) {
                            next_token(st); // void params
                        } else {
                            st->pos = saved2;
                            goto parse_fp_params;
                        }
                    } else {
                    parse_fp_params:;
                        for (;;) {
                            c_token ellip = peek_token(st);
                            if (ellip.kind == CTK_ELLIPSIS) {
                                next_token(st);
                                fp.fp_variadic = 1;
                                break;
                            }
                            if (ellip.kind == CTK_RPAREN || ellip.kind == CTK_EOF) break;
                            u32     fp_start = st->pos;
                            c_param fp_p     = parse_param(st);
                            if (st->pos == fp_start) break;
                            array_push(fps, fp_p);
                            c_token sep = peek_token(st);
                            if (sep.kind == CTK_COMMA) {
                                next_token(st);
                            } else {
                                break;
                            }
                        }
                        c_token rp = peek_token(st);
                        if (rp.kind == CTK_RPAREN) next_token(st);
                    }
                    fp.fp_params      = fps.v;
                    fp.fp_param_count = fps.size;
                    p.type            = fp;
                    return p;
                }
            }
        }
        // not a function pointer, restore
        st->pos = saved;
    }

    // regular param with optional name
    tok = peek_token(st);
    if (tok.kind == CTK_IDENT) {
        // check it's not the start of the next param's type
        // heuristic: if followed by , or ) or [ it's a name
        u32     saved2 = st->pos;
        c_token ident  = next_token(st);
        c_token after  = peek_token(st);
        if (after.kind == CTK_COMMA || after.kind == CTK_RPAREN || after.kind == CTK_LBRACKET) {
            p.name = tok_str(st->alloc, ident);
        } else {
            st->pos = saved2;
        }
    }

    // skip array declarators: [N]
    tok = peek_token(st);
    if (tok.kind == CTK_LBRACKET) {
        next_token(st);
        while (st->pos < st->src_len) {
            c_token x = next_token(st);
            if (x.kind == CTK_RBRACKET || x.kind == CTK_EOF) break;
        }
        // arrays decay to pointers in function params
        p.type.pointer_depth++;
    }

    return p;
}

// ---------------------------------------------------------------------------
// Top-level parsing
// ---------------------------------------------------------------------------

static int parse_define(cbind_state *st) {
    // We're right after #define. Expect: NAME [value]
    c_token name = peek_token(st);
    if (name.kind != CTK_IDENT) {
        skip_to_end_of_line(st);
        return 1;
    }
    next_token(st);

    // skip function-like macros: #define NAME(
    c_token after = peek_token(st);
    if (after.kind == CTK_LPAREN && after.start == name.start + name.len) {
        // no space before ( means function-like macro
        skip_to_end_of_line(st);
        return 1;
    }

    if (after.kind == CTK_NEWLINE || after.kind == CTK_EOF) {
        // empty define, skip
        return 1;
    }

    // Only emit constants from the target file
    if (!st->in_target) {
        skip_to_end_of_line(st);
        return 1;
    }

    // Skip defines that start with _ (internal/reserved)
    if (name.start[0] == '_') {
        skip_to_end_of_line(st);
        return 1;
    }

    // Only emit simple integer constants: #define NAME <number>
    c_token val = peek_token(st);
    if (val.kind == CTK_NUMBER) {
        c_decl d = {0};
        d.kind   = CDECL_DEFINE;
        d.name   = tok_str(st->alloc, name);
        array_push(st->decls, d);
    }

    skip_to_end_of_line(st);
    return 1;
}

// Parse array dimension(s) from a struct field declarator: [N] or [N][M]...
// Sets t->array_size to the first dimension. Consumes all bracket pairs
// so multi-dimensional arrays don't leave tokens in the stream.
static void parse_field_array(cbind_state *st, c_type *t) {
    while (peek_token(st).kind == CTK_LBRACKET) {
        next_token(st); // consume [
        c_token size_tok = peek_token(st);
        if (size_tok.kind == CTK_NUMBER) {
            next_token(st);
            if (t->array_size == 0) {
                // use a bounded parse: token is not null-terminated
                char buf[32];
                u32  n = size_tok.len < sizeof(buf) - 1 ? size_tok.len : (u32)(sizeof(buf) - 1);
                memcpy(buf, size_tok.start, n);
                buf[n]        = '\0';
                t->array_size = (u32)strtoul(buf, NULL, 10);
            }
        }
        while (peek_token(st).kind != CTK_RBRACKET && peek_token(st).kind != CTK_EOF) next_token(st);
        if (peek_token(st).kind == CTK_RBRACKET) next_token(st);
    }
}

static void parse_struct_or_union(cbind_state *st, int is_target, str tag_name, int is_typedef,
                                  str *out_typedef_name) {
    c_token brace = peek_token(st);
    if (brace.kind == CTK_LBRACE) {
        next_token(st); // consume {

        field_array fields = {.alloc = st->alloc};

        while (st->pos < st->src_len) {
            u32 field_loop_pos = st->pos;
            skip_all_attributes(st);
            c_token check = peek_token(st);
            if (check.kind == CTK_RBRACE) {
                next_token(st);
                break;
            }
            if (check.kind == CTK_EOF) break;

            // skip #define directives inside struct body (e.g. glibc's fd_set)
            if (check.kind == CTK_HASH) {
                next_token(st); // consume #
                skip_to_end_of_line(st);
                continue;
            }

            // parse field: type name; or type (*name)(params);
            c_type ft = parse_type(st);

            skip_all_attributes(st);
            c_token fname = peek_token(st);

            // function pointer field: type (*name)(params)
            if (fname.kind == CTK_LPAREN) {
                u32 fp_saved = st->pos;
                next_token(st); // consume (
                c_token star = peek_token(st);
                if (star.kind == CTK_STAR) {
                    next_token(st); // consume *
                    c_token fp_name = peek_token(st);
                    if (fp_name.kind == CTK_IDENT) {
                        next_token(st);
                        c_token rp = peek_token(st);
                        if (rp.kind == CTK_RPAREN) {
                            next_token(st); // consume )
                            if (peek_token(st).kind == CTK_LPAREN) {
                                next_token(st); // consume (
                                // build function pointer type with ft as return type
                                c_type fp       = {0};
                                fp.kind         = CT_FUNC_PTR;
                                fp.fp_ret       = alloc_malloc(st->alloc, sizeof(c_type));
                                *fp.fp_ret      = ft;
                                param_array fps = {.alloc = st->alloc};
                                c_token     chk = peek_token(st);
                                if (chk.kind == CTK_RPAREN) {
                                    next_token(st);
                                } else if (chk.kind == CTK_IDENT && tok_eq(chk, "void")) {
                                    u32 vs = st->pos;
                                    next_token(st);
                                    if (peek_token(st).kind == CTK_RPAREN) {
                                        next_token(st);
                                    } else {
                                        st->pos = vs;
                                        goto parse_fp_field_params;
                                    }
                                } else {
                                parse_fp_field_params:;
                                    for (;;) {
                                        c_token e = peek_token(st);
                                        if (e.kind == CTK_RPAREN || e.kind == CTK_EOF) break;
                                        if (e.kind == CTK_ELLIPSIS) {
                                            next_token(st);
                                            fp.fp_variadic = 1;
                                            break;
                                        }
                                        u32     ps = st->pos;
                                        c_param pp = parse_param(st);
                                        if (st->pos == ps) break;
                                        array_push(fps, pp);
                                        if (peek_token(st).kind == CTK_COMMA) next_token(st);
                                        else break;
                                    }
                                    if (peek_token(st).kind == CTK_RPAREN) next_token(st);
                                }
                                fp.fp_params      = fps.v;
                                fp.fp_param_count = fps.size;
                                c_field f         = {.name = tok_str(st->alloc, fp_name), .type = fp};
                                array_push(fields, f);
                                c_token semi = peek_token(st);
                                if (semi.kind == CTK_SEMICOLON) next_token(st);
                                continue;
                            }
                        }
                    }
                }
                // not a function pointer — skip to semicolon
                st->pos = fp_saved;
                skip_to_semicolon(st);
                continue;
            }

            if (fname.kind == CTK_IDENT) {
                next_token(st);
                c_field f = {.name = tok_str(st->alloc, fname), .type = ft};

                // skip bit fields: : N
                c_token colon = peek_token(st);
                if (colon.kind == CTK_COLON) {
                    next_token(st);
                    c_token bits = peek_token(st);
                    if (bits.kind == CTK_NUMBER) next_token(st);
                }

                parse_field_array(st, &f.type);

                array_push(fields, f);

                // handle comma-separated declarators: int *a, b, **c;
                // strip pointer info from base type — each declarator has its own
                {
                    c_type base        = ft;
                    base.pointer_depth = 0;
                    base.is_const      = 0;
                    memset(base.const_at, 0, sizeof(base.const_at));
                    for (;;) {
                        c_token sep = peek_token(st);
                        if (sep.kind != CTK_COMMA) break;
                        next_token(st);
                        // parse per-declarator pointer depth
                        c_type field_type = base;
                        while (peek_token(st).kind == CTK_STAR) {
                            next_token(st);
                            field_type.pointer_depth++;
                        }
                        c_token next_name = peek_token(st);
                        if (next_name.kind == CTK_IDENT) {
                            next_token(st);
                            c_field f2 = {.name = tok_str(st->alloc, next_name), .type = field_type};
                            parse_field_array(st, &f2.type);
                            array_push(fields, f2);
                        } else {
                            break;
                        }
                    }
                }
            } else if (fname.kind == CTK_SEMICOLON) {
                // anonymous struct/union member, skip
                next_token(st);
                continue;
            } else {
                // skip unknown
                skip_to_semicolon(st);
                continue;
            }

            c_token semi = peek_token(st);
            if (semi.kind == CTK_SEMICOLON) next_token(st);

            // safety: if no progress was made, skip a token to avoid infinite loop
            if (st->pos == field_loop_pos) next_token(st);
        }

        // record that this struct has been defined
        if (!str_is_empty(tag_name)) {
            int one = 1;
            str_map_set(&st->struct_defs, tag_name, &one);
        }

        // If this is a typedef, consume the typedef name
        if (is_typedef) {
            skip_all_attributes(st);
            c_token tname = peek_token(st);
            if (tname.kind == CTK_STAR) {
                // typedef struct foo *foo_ptr; — skip pointer typedefs for now
                skip_to_semicolon(st);
                // still emit the struct if from target
                if (is_target && !str_is_empty(tag_name)) {
                    c_decl d      = {0};
                    d.kind        = CDECL_STRUCT;
                    d.name        = tag_name;
                    d.fields      = fields.v;
                    d.field_count = fields.size;
                    array_push(st->decls, d);
                }
                return;
            }
            if (tname.kind == CTK_IDENT) {
                next_token(st);
                str typedef_name = tok_str(st->alloc, tname);
                if (out_typedef_name) *out_typedef_name = typedef_name;

                // register typedef
                if (str_is_empty(tag_name)) {
                    // anonymous struct: typedef struct { ... } foo;
                    // emit as c_foo
                    str tess_name =
                      str_fmt(st->alloc, "c_%.*s", str_ilen(typedef_name), str_buf(&typedef_name));
                    str_map_set(&st->typedefs, typedef_name, &tess_name);

                    if (is_target) {
                        c_decl d      = {0};
                        d.kind        = CDECL_STRUCT;
                        d.name        = typedef_name; // use typedef name, not tag
                        d.fields      = fields.v;
                        d.field_count = fields.size;
                        array_push(st->decls, d);
                    }
                } else {
                    // named struct: typedef struct tag { ... } name;
                    str tess_name =
                      str_fmt(st->alloc, "c_%.*s", str_ilen(typedef_name), str_buf(&typedef_name));
                    str_map_set(&st->typedefs, typedef_name, &tess_name);

                    if (is_target) {
                        c_decl d      = {0};
                        d.kind        = CDECL_STRUCT;
                        d.name        = tag_name;
                        d.fields      = fields.v;
                        d.field_count = fields.size;
                        array_push(st->decls, d);
                    }
                }
            }
            // consume semicolon
            c_token semi = peek_token(st);
            if (semi.kind == CTK_SEMICOLON) next_token(st);
            return;
        }

        // not a typedef, just a struct definition
        if (is_target && !str_is_empty(tag_name)) {
            c_decl d      = {0};
            d.kind        = CDECL_STRUCT;
            d.name        = tag_name;
            d.fields      = fields.v;
            d.field_count = fields.size;
            array_push(st->decls, d);
        }

        // consume trailing ;
        c_token semi = peek_token(st);
        if (semi.kind == CTK_SEMICOLON) next_token(st);
    } else {
        // forward declaration or just "struct tag" in a typedef
        if (is_typedef) {
            // typedef struct tag name;
            // or: typedef struct tag *name;
            skip_all_attributes(st);
            c_token p = peek_token(st);
            // skip pointer
            int ptr = 0;
            while (p.kind == CTK_STAR) {
                next_token(st);
                ptr++;
                p = peek_token(st);
            }
            if (p.kind == CTK_IDENT) {
                next_token(st);
                str typedef_name = tok_str(st->alloc, p);
                if (out_typedef_name) *out_typedef_name = typedef_name;

                if (ptr == 0 && !str_is_empty(tag_name)) {
                    str tess_name =
                      str_fmt(st->alloc, "c_%.*s", str_ilen(typedef_name), str_buf(&typedef_name));
                    str_map_set(&st->typedefs, typedef_name, &tess_name);
                }
                // If not defined yet, register forward decl
                if (!str_is_empty(tag_name) && !str_map_contains(st->struct_defs, tag_name)) {
                    if (is_target && !str_hset_contains(st->forward_decls, tag_name)) {
                        str_hset_insert(&st->forward_decls, tag_name);
                        c_decl d = {0};
                        d.kind   = CDECL_FORWARD_STRUCT;
                        d.name   = tag_name;
                        array_push(st->decls, d);
                    }
                }
            }
            c_token semi = peek_token(st);
            if (semi.kind == CTK_SEMICOLON) next_token(st);
        } else {
            // bare forward decl: struct tag;
            if (!str_is_empty(tag_name) && !str_map_contains(st->struct_defs, tag_name)) {
                if (is_target && !str_hset_contains(st->forward_decls, tag_name)) {
                    str_hset_insert(&st->forward_decls, tag_name);
                    c_decl d = {0};
                    d.kind   = CDECL_FORWARD_STRUCT;
                    d.name   = tag_name;
                    array_push(st->decls, d);
                }
            }
            c_token semi = peek_token(st);
            if (semi.kind == CTK_SEMICOLON) next_token(st);
        }
    }
}

static void parse_enum_body(cbind_state *st, int is_target, int is_typedef) {
    c_token brace = peek_token(st);
    if (brace.kind != CTK_LBRACE) {
        // enum without body
        if (is_typedef) {
            skip_to_semicolon(st);
        } else {
            c_token semi = peek_token(st);
            if (semi.kind == CTK_SEMICOLON) next_token(st);
        }
        return;
    }
    next_token(st); // consume {

    while (st->pos < st->src_len) {

        skip_all_attributes(st);
        c_token check = peek_token(st);
        if (check.kind == CTK_RBRACE) {
            next_token(st);
            break;
        }
        if (check.kind == CTK_EOF) break;

        if (check.kind == CTK_IDENT) {
            next_token(st);
            str ename = tok_str(st->alloc, check);

            // optional = value
            c_token eq = peek_token(st);
            if (eq.kind == CTK_EQUALS) {
                next_token(st);
                // skip value expression (could be complex)
                int depth = 0;
                while (st->pos < st->src_len) {
                    c_token v = peek_token(st);
                    if (v.kind == CTK_LPAREN) {
                        next_token(st);
                        depth++;
                    } else if (v.kind == CTK_RPAREN) {
                        if (depth > 0) {
                            next_token(st);
                            depth--;
                        } else {
                            break;
                        }
                    } else if ((v.kind == CTK_COMMA || v.kind == CTK_RBRACE) && depth == 0) {
                        break;
                    } else if (v.kind == CTK_EOF) {
                        break;
                    } else {
                        next_token(st);
                    }
                }
            }

            if (is_target) {
                c_decl d = {0};
                d.kind   = CDECL_ENUM_VALUE;
                d.name   = ename;
                array_push(st->decls, d);
            }
        }

        c_token sep = peek_token(st);
        if (sep.kind == CTK_COMMA) next_token(st);
    }

    // if typedef, consume typedef name
    if (is_typedef) {
        skip_all_attributes(st);
        c_token tname = peek_token(st);
        if (tname.kind == CTK_IDENT) {
            next_token(st);
            // register typedef as CInt
            str typedef_name = tok_str(st->alloc, tname);
            str tess_name    = str_init(st->alloc, "CInt");
            str_map_set(&st->typedefs, typedef_name, &tess_name);
        }
    }

    c_token semi = peek_token(st);
    if (semi.kind == CTK_SEMICOLON) next_token(st);
}

static int parse_toplevel_decl(cbind_state *st) {
    skip_all_attributes(st);
    c_token first = peek_token(st);

    if (first.kind == CTK_EOF) return 0;
    if (first.kind == CTK_SEMICOLON) {
        next_token(st);
        return 1;
    }

    // #define directive (line markers are auto-handled by next_token)
    if (first.kind == CTK_HASH) {
        next_token(st);
        c_token directive = peek_token(st);
        if (directive.kind == CTK_IDENT && tok_eq(directive, "define")) {
            next_token(st);
            return parse_define(st);
        }
        // skip other preprocessor directives (#undef, #pragma, etc.)
        skip_to_end_of_line(st);
        return 1;
    }

    // extern "C" { ... } — skip the wrapper
    if (first.kind == CTK_IDENT && tok_eq(first, "extern")) {
        next_token(st);
        c_token next = peek_token(st);
        if (next.kind == CTK_STRING && tok_eq(next, "\"C\"")) {
            next_token(st);
            c_token lb = peek_token(st);
            if (lb.kind == CTK_LBRACE) next_token(st); // consume {
            return 1;
        }
        // extern type func(...); — skip extern keyword, fall through to type parsing
    }

    // static — consume and fall through (handles static inline functions)
    if (first.kind == CTK_IDENT && tok_eq(first, "static")) {
        next_token(st);
        first = peek_token(st); // re-peek so subsequent checks see what follows
    }

    // _Static_assert, static_assert
    if (first.kind == CTK_IDENT && (tok_eq(first, "_Static_assert") || tok_eq(first, "static_assert"))) {
        next_token(st);
        skip_to_semicolon(st);
        return 1;
    }

    // typedef
    if (first.kind == CTK_IDENT && tok_eq(first, "typedef")) {
        next_token(st);
        skip_all_attributes(st);
        c_token after = peek_token(st);

        if (after.kind == CTK_IDENT && (tok_eq(after, "struct") || tok_eq(after, "union"))) {
            next_token(st);
            c_token tag      = peek_token(st);
            str     tag_name = str_empty();
            if (tag.kind == CTK_IDENT && !tok_eq(tag, "const") && !tok_eq(tag, "volatile")) {
                // check if next is { (tag name) or if this IS the body start
                next_token(st);
                tag_name = tok_str(st->alloc, tag);
            }
            parse_struct_or_union(st, st->in_target, tag_name, 1, NULL);
            return 1;
        }
        if (after.kind == CTK_IDENT && tok_eq(after, "enum")) {
            next_token(st);
            c_token tag = peek_token(st);
            if (tag.kind == CTK_IDENT) {
                // consume enum tag name if present
                u32 save2 = st->pos;
                next_token(st);
                c_token after_name = peek_token(st);
                if (after_name.kind == CTK_LBRACE || after_name.kind == CTK_IDENT ||
                    after_name.kind == CTK_SEMICOLON) {
                    // consumed tag name, continue
                } else {
                    st->pos = save2;
                }
            }
            parse_enum_body(st, st->in_target, 1);
            return 1;
        }

        // typedef for function pointer: typedef ret (*name)(params);
        // typedef for simple type: typedef type name;
        c_type aliased = parse_type(st);
        skip_all_attributes(st);

        c_token next = peek_token(st);
        if (next.kind == CTK_LPAREN) {
            // could be function pointer typedef
            next_token(st); // (
            c_token star = peek_token(st);
            if (star.kind == CTK_STAR) {
                next_token(st);
                c_token fp_name = peek_token(st);
                if (fp_name.kind == CTK_IDENT) {
                    next_token(st);
                    str     typedef_name = tok_str(st->alloc, fp_name);
                    c_token rp           = peek_token(st);
                    if (rp.kind == CTK_RPAREN) {
                        next_token(st);
                        // skip param list — we register the typedef as a named type
                        // (function pointer signature is not preserved yet)
                        skip_paren_block(st);
                        str tess =
                          str_fmt(st->alloc, "c_%.*s", str_ilen(typedef_name), str_buf(&typedef_name));
                        str_map_set(&st->typedefs, typedef_name, &tess);
                    }
                }
            }
            // skip to semicolon regardless
            skip_to_semicolon(st);
            return 1;
        }

        // simple typedef: typedef type name;
        if (next.kind == CTK_IDENT) {
            next_token(st);
            str typedef_name = tok_str(st->alloc, next);

            // resolve the aliased type to a tess type string
            str tess = type_to_tess(st->alloc, &aliased, st->typedefs);
            str_map_set(&st->typedefs, typedef_name, &tess);
        }

        skip_to_semicolon(st);
        return 1;
    }

    // struct/union definition (not typedef) — but only if followed by { or ;
    // If followed by * or identifier, it's a return type for a function declaration
    if (first.kind == CTK_IDENT && (tok_eq(first, "struct") || tok_eq(first, "union"))) {
        u32 saved_struct = st->pos;
        next_token(st);
        c_token tag      = peek_token(st);
        str     tag_name = str_empty();
        if (tag.kind == CTK_IDENT) {
            next_token(st);
            tag_name = tok_str(st->alloc, tag);
        }
        c_token after_tag = peek_token(st);
        if (after_tag.kind == CTK_LBRACE || after_tag.kind == CTK_SEMICOLON) {
            parse_struct_or_union(st, st->in_target, tag_name, 0, NULL);
            return 1;
        }
        // not a struct definition — restore and fall through to function parsing
        st->pos = saved_struct;
    }

    // enum definition (not typedef)
    if (first.kind == CTK_IDENT && tok_eq(first, "enum")) {
        next_token(st);
        c_token tag = peek_token(st);
        if (tag.kind == CTK_IDENT) {
            next_token(st); // consume tag name
        }
        parse_enum_body(st, st->in_target, 0);
        return 1;
    }

    // Otherwise: try to parse as function declaration
    // type name(params);
    if (first.kind == CTK_IDENT || first.kind == CTK_STAR) {
        u32    saved = st->pos;
        c_type ret   = parse_type(st);

        skip_all_attributes(st);
        c_token name_tok = peek_token(st);

        if (name_tok.kind == CTK_IDENT) {
            next_token(st);
            str fname = tok_str(st->alloc, name_tok);

            skip_all_attributes(st);
            c_token lp = peek_token(st);

            if (lp.kind == CTK_LPAREN) {
                next_token(st); // consume (

                param_array params   = {.alloc = st->alloc};
                int         variadic = 0;

                c_token     check    = peek_token(st);
                if (check.kind == CTK_RPAREN) {
                    next_token(st); // empty params
                } else if (check.kind == CTK_IDENT && tok_eq(check, "void")) {
                    u32     s2 = st->pos;
                    c_token v  = next_token(st);
                    (void)v;
                    c_token a2 = peek_token(st);
                    if (a2.kind == CTK_RPAREN) {
                        next_token(st); // void means no params
                    } else {
                        st->pos = s2;
                        goto parse_func_params;
                    }
                } else {
                parse_func_params:;
                    for (;;) {

                        skip_all_attributes(st);
                        c_token e = peek_token(st);
                        if (e.kind == CTK_ELLIPSIS) {
                            next_token(st);
                            variadic = 1;
                            break;
                        }
                        if (e.kind == CTK_RPAREN || e.kind == CTK_EOF) break;
                        u32     param_start = st->pos;
                        c_param p           = parse_param(st);
                        if (st->pos == param_start) break; // no progress
                        array_push(params, p);

                        c_token sep = peek_token(st);
                        if (sep.kind == CTK_COMMA) next_token(st);
                        else break;
                    }
                    c_token rp = peek_token(st);
                    if (rp.kind == CTK_RPAREN) next_token(st);
                }

                skip_all_attributes(st);

                // check for function body (inline function) — skip it
                c_token after = peek_token(st);
                if (after.kind == CTK_LBRACE) {
                    next_token(st);
                    skip_braced_block(st);
                    // don't emit inline functions
                    return 1;
                }

                // consume semicolon
                if (after.kind == CTK_SEMICOLON) next_token(st);

                if (st->in_target) {
                    // Skip internal functions with __ prefix
                    if (str_len(fname) >= 2 && str_buf(&fname)[0] == '_' && str_buf(&fname)[1] == '_')
                        return 1;

                    c_decl d      = {0};
                    d.kind        = CDECL_FUNCTION;
                    d.name        = fname;
                    d.return_type = ret;
                    d.params      = params.v;
                    d.param_count = params.size;
                    d.is_variadic = variadic;
                    array_push(st->decls, d);
                }
                return 1;
            }

            // not a function — maybe a variable declaration, skip
            skip_to_semicolon(st);
            return 1;
        }

        // couldn't parse, skip to semicolon
        st->pos = saved;
        skip_to_semicolon(st);
        return 1;
    }

    // closing brace from extern "C" { }
    if (first.kind == CTK_RBRACE) {
        next_token(st);
        return 1;
    }

    // skip anything else
    skip_to_semicolon(st);
    return 1;
}

static void parse_all(cbind_state *st) {
    while (st->pos < st->src_len) {
        u32 prev_pos = st->pos;
        if (!parse_toplevel_decl(st)) break;
        if (st->pos == prev_pos) {
            // parser made no progress — skip one token to avoid infinite loop
            next_token(st);
        }
    }
}

// ---------------------------------------------------------------------------
// Type-to-Tess conversion
// ---------------------------------------------------------------------------

static char const *base_type_to_tess(c_type_kind kind) {
    switch (kind) {
    case CT_VOID:       return "Void";
    case CT_CHAR:       return "CChar";
    case CT_SCHAR:      return "CChar";
    case CT_UCHAR:      return "CUnsignedChar";
    case CT_SHORT:      return "CShort";
    case CT_USHORT:     return "CUnsignedShort";
    case CT_INT:        return "CInt";
    case CT_UINT:       return "CUnsignedInt";
    case CT_LONG:       return "CLong";
    case CT_ULONG:      return "CUnsignedLong";
    case CT_LONGLONG:   return "CLongLong";
    case CT_ULONGLONG:  return "CUnsignedLongLong";
    case CT_FLOAT:      return "CFloat";
    case CT_DOUBLE:     return "CDouble";
    case CT_LONGDOUBLE: return "CLongDouble";
    case CT_BOOL:       return "Bool";
    case CT_SIZE_T:     return "CSize";
    case CT_SSIZE_T:    return "CPtrDiff"; // closest match
    case CT_PTRDIFF_T:  return "CPtrDiff";
    case CT_INT8:       return "CInt8";
    case CT_UINT8:      return "CUInt8";
    case CT_INT16:      return "CInt16";
    case CT_UINT16:     return "CUInt16";
    case CT_INT32:      return "CInt32";
    case CT_UINT32:     return "CUInt32";
    case CT_INT64:      return "CInt64";
    case CT_UINT64:     return "CUInt64";
    case CT_INTPTR:     return "CPtrDiff";
    case CT_UINTPTR:    return "CSize";
    case CT_NAMED:      return NULL;
    case CT_FUNC_PTR:   return NULL;
    case CT_UNKNOWN:    return NULL;
    }
    return NULL;
}

static str resolve_named_type(allocator *a, str name, hashmap *typedefs) {
    // check if it's a known struct
    if (str_len(name) > 7 && 0 == memcmp(str_buf(&name), "struct ", 7)) {
        // struct tag -> c_struct_tag
        cspan sp = str_cspan(&name);
        return str_fmt(a, "c_struct_%.*s", (int)(sp.len - 7), sp.buf + 7);
    }
    if (str_len(name) > 6 && 0 == memcmp(str_buf(&name), "union ", 6)) {
        cspan sp = str_cspan(&name);
        return str_fmt(a, "c_union_%.*s", (int)(sp.len - 6), sp.buf + 6);
    }

    // check typedefs map
    if (typedefs) {
        str *found = str_map_get(typedefs, name);
        if (found) return str_copy(a, *found);
    }

    // unknown — emit as c_name
    return str_fmt(a, "c_%.*s", str_ilen(name), str_buf(&name));
}

static str type_to_tess(allocator *a, c_type const *t, hashmap *typedefs) {
    // function pointer type
    if (t->kind == CT_FUNC_PTR && t->fp_ret) {
        str_build sb = str_build_init(a, 64);
        str       lp = str_init_static("(");
        str_build_cat(&sb, lp);
        for (u32 i = 0; i < t->fp_param_count; i++) {
            if (i > 0) {
                str comma = str_init_static(", ");
                str_build_cat(&sb, comma);
            }
            str pname;
            if (!str_is_empty(t->fp_params[i].name)) {
                pname = t->fp_params[i].name;
            } else {
                pname = str_fmt(a, "arg%u", i);
            }
            str ptype = type_to_tess(a, &t->fp_params[i].type, typedefs);
            str part =
              str_fmt(a, "%.*s: %.*s", str_ilen(pname), str_buf(&pname), str_ilen(ptype), str_buf(&ptype));
            str_build_cat(&sb, part);
        }
        str ret        = type_to_tess(a, t->fp_ret, typedefs);
        str arrow_part = str_fmt(a, ") -> %.*s", str_ilen(ret), str_buf(&ret));
        str_build_cat(&sb, arrow_part);
        return str_build_finish(&sb);
    }

    // fixed-size array
    if (t->array_size > 0) {
        c_type elem     = *t;
        elem.array_size = 0;
        str elem_str    = type_to_tess(a, &elem, typedefs);
        return str_fmt(a, "CArray[%.*s, %u]", str_ilen(elem_str), str_buf(&elem_str), t->array_size);
    }

    // base type name
    str         base;
    char const *builtin = base_type_to_tess(t->kind);
    if (builtin) {
        base = str_init_static(builtin);
    } else if (t->kind == CT_NAMED) {
        base = resolve_named_type(a, t->name, typedefs);
    } else {
        base = str_init_static("any");
    }

    // special case: char* -> CString (non-const)
    if (t->kind == CT_CHAR && t->pointer_depth == 1 && !t->is_const) {
        return str_init_static("CString");
    }

    // special case: void* -> Ptr[any]
    if (t->kind == CT_VOID && t->pointer_depth > 0) {
        str result = str_init_static("any");
        if (t->is_const) {
            result = str_fmt(a, "Const[any]");
        }
        for (int i = 0; i < t->pointer_depth; i++) {
            result = str_fmt(a, "Ptr[%.*s]", str_ilen(result), str_buf(&result));
        }
        return result;
    }

    if (t->pointer_depth == 0) {
        if (t->is_const) {
            return str_fmt(a, "Const[%.*s]", str_ilen(base), str_buf(&base));
        }
        return base;
    }

    // build pointer chain from inside out
    str inner = base;
    if (t->is_const) {
        inner = str_fmt(a, "Const[%.*s]", str_ilen(inner), str_buf(&inner));
    }
    for (int i = 0; i < t->pointer_depth; i++) {
        inner = str_fmt(a, "Ptr[%.*s]", str_ilen(inner), str_buf(&inner));
        if (i < 4 && t->const_at[i]) {
            inner = str_fmt(a, "Const[%.*s]", str_ilen(inner), str_buf(&inner));
        }
    }
    return inner;
}

// ---------------------------------------------------------------------------
// Emitter
// ---------------------------------------------------------------------------

static str emit_bindings(allocator *a, cbind_state *st, char const *module_name) {
    str_build sb      = str_build_init(a, 4096);
    hashmap  *emitted = hset_create(a, 64);

    // module + include
    str mod_line = str_fmt(a, "#module %s\n", module_name);
    str_build_cat(&sb, mod_line);

    // determine include style
    str inc_line;
    if (str_contains_char(st->target_file, '/') || str_contains_char(st->target_file, '\\')) {
        inc_line =
          str_fmt(a, "#include \"%.*s\"\n\n", str_ilen(st->target_file), str_buf(&st->target_file));
    } else {
        inc_line = str_fmt(a, "#include <%.*s>\n\n", str_ilen(st->target_file), str_buf(&st->target_file));
    }
    str_build_cat(&sb, inc_line);

    // Emit forward-declared (opaque) structs
    int has_fwd = 0;
    forall(i, st->decls) {
        c_decl *d = &st->decls.v[i];
        if (d->kind != CDECL_FORWARD_STRUCT) continue;
        if (str_hset_contains(emitted, d->name)) continue;
        // skip if this struct was later fully defined (O(1) hashmap check)
        if (!str_map_contains(st->struct_defs, d->name)) {
            str_hset_insert(&emitted, d->name);
            str line =
              str_fmt(a, "c_struct_%.*s: { _opaque: Byte }\n", str_ilen(d->name), str_buf(&d->name));
            str_build_cat(&sb, line);
            has_fwd = 1;
        }
    }

    // Emit struct definitions
    int has_struct = 0;
    forall(i, st->decls) {
        c_decl *d = &st->decls.v[i];
        if (d->kind != CDECL_STRUCT) continue;
        if (str_hset_contains(emitted, d->name)) continue;
        str_hset_insert(&emitted, d->name);

        // determine prefix: if name doesn't look like a raw tag, it's from an anonymous typedef
        int is_tag = 0;
        // check if there's a struct def entry
        if (str_map_contains(st->struct_defs, d->name)) is_tag = 1;

        str prefix;
        if (is_tag) {
            prefix = str_fmt(a, "c_struct_%.*s", str_ilen(d->name), str_buf(&d->name));
        } else {
            prefix = str_fmt(a, "c_%.*s", str_ilen(d->name), str_buf(&d->name));
        }

        str_build line_sb = str_build_init(a, 128);
        str       lpart   = str_fmt(a, "%.*s: { ", str_ilen(prefix), str_buf(&prefix));
        str_build_cat(&line_sb, lpart);

        for (u32 f = 0; f < d->field_count; f++) {
            if (f > 0) {
                str comma = str_init_static(", ");
                str_build_cat(&line_sb, comma);
            }
            str ftype = type_to_tess(a, &d->fields[f].type, st->typedefs);
            str fpart = str_fmt(a, "%.*s: %.*s", str_ilen(d->fields[f].name), str_buf(&d->fields[f].name),
                                str_ilen(ftype), str_buf(&ftype));
            str_build_cat(&line_sb, fpart);
        }

        str close = str_init_static(" }\n");
        str_build_cat(&line_sb, close);
        str line = str_build_finish(&line_sb);
        str_build_cat(&sb, line);
        has_struct = 1;
    }

    if (has_fwd || has_struct) {
        str nl = str_init_static("\n");
        str_build_cat(&sb, nl);
    }

    // Emit enum values and #define constants
    int has_const = 0;
    forall(i, st->decls) {
        c_decl *d = &st->decls.v[i];
        if (d->kind == CDECL_ENUM_VALUE || d->kind == CDECL_DEFINE) {
            if (str_hset_contains(emitted, d->name)) continue;
            str_hset_insert(&emitted, d->name);
            str line = str_fmt(a, "c_%.*s: CInt\n", str_ilen(d->name), str_buf(&d->name));
            str_build_cat(&sb, line);
            has_const = 1;
        }
    }
    if (has_const) {
        str nl = str_init_static("\n");
        str_build_cat(&sb, nl);
    }

    // Emit function declarations
    forall(i, st->decls) {
        c_decl *d = &st->decls.v[i];
        if (d->kind != CDECL_FUNCTION) continue;
        if (str_hset_contains(emitted, d->name)) continue;
        str_hset_insert(&emitted, d->name);

        str_build fn_sb = str_build_init(a, 128);
        str       fname = str_fmt(a, "c_%.*s(", str_ilen(d->name), str_buf(&d->name));
        str_build_cat(&fn_sb, fname);

        for (u32 p = 0; p < d->param_count; p++) {
            if (p > 0) {
                str comma = str_init_static(", ");
                str_build_cat(&fn_sb, comma);
            }
            str pname;
            if (!str_is_empty(d->params[p].name)) {
                pname = d->params[p].name;
                // Strip leading __ from parameter names
                if (str_len(pname) > 2 && str_buf(&pname)[0] == '_' && str_buf(&pname)[1] == '_')
                    pname = str_fmt(a, "%.*s", str_ilen(pname) - 2, str_buf(&pname) + 2);
            } else {
                pname = str_fmt(a, "arg%u", p);
            }
            str ptype = type_to_tess(a, &d->params[p].type, st->typedefs);
            str ppart =
              str_fmt(a, "%.*s: %.*s", str_ilen(pname), str_buf(&pname), str_ilen(ptype), str_buf(&ptype));
            str_build_cat(&fn_sb, ppart);
        }

        if (d->is_variadic) {
            if (d->param_count > 0) {
                str comma = str_init_static(", ");
                str_build_cat(&fn_sb, comma);
            }
            str va = str_init_static("args: ...");
            str_build_cat(&fn_sb, va);
        }

        str ret  = type_to_tess(a, &d->return_type, st->typedefs);
        str tail = str_fmt(a, ") -> %.*s\n", str_ilen(ret), str_buf(&ret));
        str_build_cat(&fn_sb, tail);

        str fn_line = str_build_finish(&fn_sb);
        str_build_cat(&sb, fn_line);
    }

    return str_build_finish(&sb);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

str tl_cbind_from_preprocessed(allocator *alloc, char const *pp_output, u32 pp_len, char const *target_file,
                               char const *module_name) {
    cbind_state st     = {0};
    st.alloc           = alloc;
    st.src             = pp_output;
    st.src_len         = pp_len;
    st.pos             = 0;
    st.target_file     = str_init(alloc, target_file);
    st.target_basename = file_basename(target_file);
    st.in_target       = 0;
    st.decls           = (decl_array){.alloc = alloc};
    st.typedefs        = map_create(alloc, sizeof(str), 64);
    st.struct_defs     = map_create(alloc, sizeof(int), 64);
    st.forward_decls   = hset_create(alloc, 32);
    st.verbose         = 0;

    parse_all(&st);

    return emit_bindings(alloc, &st, module_name);
}

str tl_cbind(allocator *alloc, tl_cbind_opts const *opts) {
    if (!opts->header_path) {
        fprintf(stderr, "error: cbind: no header path provided\n");
        return str_empty();
    }

    // derive module name from header filename
    char const *module_name = opts->module_name;
    char        module_buf[256];
    if (!module_name) {
        char const *base = file_basename(opts->header_path);
        char const *dot  = strrchr(base, '.');
        size_t      len  = dot ? (size_t)(dot - base) : strlen(base);
        if (len >= sizeof(module_buf)) len = sizeof(module_buf) - 1;
        memcpy(module_buf, base, len);
        module_buf[len] = '\0';
        module_name     = module_buf;
    }

    // check that we have a C compiler
    if (str_is_empty(opts->cc)) {
        fprintf(stderr, "error: cbind: no C compiler found (set CC environment variable)\n");
        return str_empty();
    }

    // create temp file for preprocessor output
    platform_temp_file pp_out;
    if (platform_temp_file_create(&pp_out, ".i")) {
        fprintf(stderr, "error: cbind: failed to create temp file\n");
        return str_empty();
    }

    // build preprocessor command: cc -E -dD -o <tempfile> <header>
    c_string_carray argv = {.alloc = alloc};
    array_reserve(argv, 8);

    str         cc_copy = opts->cc;
    char const *cc_s    = str_cstr(&cc_copy);
    char const *flag_E  = "-E";
    char const *flag_dD = "-dD";
    char const *flag_o  = "-o";
    char const *null_t  = NULL;

    char const *pp_path = pp_out.path;
    array_push(argv, cc_s);
    array_push(argv, flag_E);
    array_push(argv, flag_dD);
    array_push(argv, flag_o);
    array_push(argv, pp_path);
    array_push(argv, opts->header_path);
    array_push(argv, null_t);

    platform_exec_opts exec = {
      .argv       = (char const *const *)argv.v,
      .stdin_data = NULL,
      .stdin_len  = 0,
      .verbose    = opts->verbose,
    };

    int rc = platform_exec(&exec);
    if (rc != 0) {
        fprintf(stderr, "error: cbind: preprocessor failed (exit %d)\n", rc);
        platform_temp_file_delete(&pp_out);
        return str_empty();
    }

    // read preprocessed output
    char *data = NULL;
    u32   size = 0;
    file_read(alloc, pp_out.path, &data, &size);
    platform_temp_file_delete(&pp_out);

    if (!data) {
        fprintf(stderr, "error: cbind: failed to read preprocessor output\n");
        return str_empty();
    }

    return tl_cbind_from_preprocessed(alloc, data, size, opts->header_path, module_name);
}
