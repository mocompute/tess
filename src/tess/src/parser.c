#include "parser_internal.h"

#include "file.h"
#include "infer.h"
#include "platform.h"

#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSER_ARENA_SIZE 1024

typedef int (*parse_fun_s)(parser *, char const *);
typedef int (*parse_fun_int)(parser *, int);

int annotation_uses_type_param(ast_node *node, str param_name);

// -- allocation and deallocation --

parser *parser_create(allocator *alloc, parser_opts const *opts) {
    parser *self = alloc_malloc(alloc, sizeof(struct parser));
    alloc_zero(self);
    self->opts                         = *opts;
    self->parent_alloc                 = alloc;
    self->file_arena                   = arena_create(alloc, 64 * 1024);
    self->tokens_arena                 = arena_create(alloc, PARSER_ARENA_SIZE);
    self->ast_arena                    = arena_create(alloc, PARSER_ARENA_SIZE);
    self->transient                    = arena_create(alloc, PARSER_ARENA_SIZE);
    self->speculative                  = arena_create(alloc, PARSER_ARENA_SIZE);
    self->tokenizer                    = null;
    self->files                        = opts->files;
    self->files_index                  = 0;
    self->current_file_data.v          = null;
    self->current_file_data.size       = 0;
    self->modules_seen                 = hset_create(self->parent_alloc, 32);
    self->modules_version_seen         = hset_create(self->parent_alloc, 32);
    self->nested_type_parents          = hset_create(self->parent_alloc, 1024);
    self->tagged_union_variant_parents = hset_create(self->parent_alloc, 256);
    self->module_aliases               = map_new(self->parent_alloc, str, str, 32);
    self->nullary_variant_parents      = map_new(self->parent_alloc, str, str, 16);
    self->module_pkg_prefixes          = opts->module_pkg_prefixes;
    self->file_pkg_prefixes            = opts->file_pkg_prefixes;
    self->result                       = null;
    self->tokens                       = (token_array){0};
    self->error                        = (struct parser_error){0};
    self->tokenizer_error              = (struct tokenizer_error){0};
    self->token                        = (struct token){0};
    self->current_module               = str_empty();
    self->current_module_symbols       = hset_create(self->parent_alloc, 512);
    self->builtin_module_symbols       = hset_create(self->parent_alloc, 512);
    self->module_symbols               = map_create_ptr(self->parent_alloc, 64); // str -> hashmap*
    self->next_var_name                = 0;
    self->verbose                      = 0;
    self->indent_level                 = 0;
    self->in_function_application      = 0;
    self->skip_module                  = 0;
    self->prelude_consumed             = 0;
    self->expect_module                = 0;
    self->mode                         = mode_none;
    self->variadic_symbols             = map_new(self->parent_alloc, str, variadic_symbol_info *, 16);
    self->function_aliases             = null; // lazy-init on first alias registration

    self->tokenizer                    = null;
    self->tokens                       = (token_array){.alloc = self->tokens_arena};

    token_init(&self->token, tok_invalid);
    self->error.token     = &self->token;
    self->error.tokenizer = &self->tokenizer_error;

    return self;
}

void parser_release_temp_arenas(parser *self) {
    if (self->tokenizer) tokenizer_destroy(&self->tokenizer);
    if (self->transient) arena_destroy(&self->transient);
    if (self->speculative) arena_destroy(&self->speculative);
    if (self->tokens_arena) arena_destroy(&self->tokens_arena);
    if (self->file_arena) arena_destroy(&self->file_arena);
    self->tokens = (token_array){0};
}

void parser_destroy(parser **self) {
    parser_release_temp_arenas(*self);

    allocator *alloc = (*self)->parent_alloc;
    if ((*self)->module_symbols) hset_destroy(&(*self)->module_symbols);
    hset_destroy(&(*self)->builtin_module_symbols);
    hset_destroy(&(*self)->current_module_symbols);
    hset_destroy(&(*self)->nested_type_parents);
    hset_destroy(&(*self)->tagged_union_variant_parents);
    map_destroy(&(*self)->nullary_variant_parents);
    if ((*self)->variadic_symbols) map_destroy(&(*self)->variadic_symbols);
    if ((*self)->function_aliases) map_destroy(&(*self)->function_aliases);
    hset_destroy(&(*self)->modules_version_seen);
    hset_destroy(&(*self)->modules_seen);
    arena_destroy(&(*self)->ast_arena);
    alloc_free(alloc, *self);
    *self = null;
}

hashmap *parser_take_module_symbols(parser *self) {
    hashmap *out         = self->module_symbols;
    self->module_symbols = null;
    return out;
}

void parser_set_module_symbols(parser *self, hashmap *mod_syms) {
    map_destroy(&self->module_symbols);
    self->module_symbols = mod_syms;
}

void parser_get_arena_stats(parser *self, arena_stats *ast, arena_stats *tokens, arena_stats *temp) {
    arena_get_stats(self->ast_arena, ast);
    arena_get_stats(self->tokens_arena, tokens);

    // Aggregate temp arenas (file, transient, speculative).
    alloc_zero(temp);
    arena_stats s;
    if (self->file_arena) {
        arena_get_stats(self->file_arena, &s);
        temp->allocated += s.allocated;
        temp->capacity += s.capacity;
        temp->bucket_count += s.bucket_count;
        temp->peak_allocated += s.peak_allocated;
    }
    if (self->transient) {
        arena_get_stats(self->transient, &s);
        temp->allocated += s.allocated;
        temp->capacity += s.capacity;
        temp->bucket_count += s.bucket_count;
        temp->peak_allocated += s.peak_allocated;
    }
    if (self->speculative) {
        arena_get_stats(self->speculative, &s);
        temp->allocated += s.allocated;
        temp->capacity += s.capacity;
        temp->bucket_count += s.bucket_count;
        temp->peak_allocated += s.peak_allocated;
    }
}

// -- module --

void add_module_symbol(parser *self, ast_node *name) {
    // Keeps names before they are mangled with the module name prefix, but after they are mangled with
    // arity.
    if (ast_node_is_symbol(name)) {
        // For safety, don't add symbols which have already been mangled.
        if (name->symbol.is_module_mangled) return;

        str name_str = ast_node_str(name);

        // don't add names which are builtin names
        if (str_hset_contains(self->builtin_module_symbols, name_str)) return;

        str_hset_insert(&self->current_module_symbols, name_str);

        // Also copy builtin names to another hset
        if (str_eq(self->current_module, S("builtin")))
            str_hset_insert(&self->builtin_module_symbols, name_str);

    } else if (ast_node_is_nfa(name)) {
        add_module_symbol(self, name->named_application.name);
    }
}

// -- parser --

void set_node_file(parser *self, ast_node *node) {
    node->file = self->token.file;
    node->line = self->token.line;
    node->col  = self->token.col;
}

ast_node *parser_make_arrow(parser *self, ast_node_array params, ast_node *return_type,
                            ast_node_sized type_params) {
    ast_node *tup = ast_node_create_tuple(self->ast_arena, (ast_node_sized)array_sized(params));
    set_node_file(self, tup);
    ast_node *arrow = ast_node_create_arrow(self->ast_arena, tup, return_type, type_params);
    set_node_file(self, arrow);
    return arrow;
}

static void set_result_file(parser *self) {
    set_node_file(self, self->result);
}

int result_ast(parser *p, ast_tag tag) {
    p->result = ast_node_create(p->ast_arena, tag);
    set_result_file(p);
    return 0;
}

int result_ast_i64(parser *p, i64 val) {
    p->result = ast_node_create_i64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_u64(parser *p, u64 val) {
    p->result = ast_node_create_u64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_i64_z(parser *p, i64 val) {
    p->result = ast_node_create_i64_z(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_u64_zu(parser *p, u64 val) {
    p->result = ast_node_create_u64_zu(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_f64(parser *p, f64 val) {
    p->result = ast_node_create_f64(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_bool(parser *p, int val) {
    p->result = ast_node_create_bool(p->ast_arena, val);
    set_result_file(p);
    return 0;
}

int result_ast_str(parser *p, ast_tag tag, char const *s) {
    p->result      = ast_node_create_sym_c(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

int result_ast_str_(parser *p, ast_tag tag, str s) {
    p->result      = ast_node_create_sym(p->ast_arena, s);
    p->result->tag = tag;
    set_result_file(p);
    return 0;
}

int result_ast_node(parser *p, ast_node *node) {
    p->result = node;
    if (!node->file || !*node->file) set_result_file(p);
    return 0;
}

int is_reserved(char const *s) {
    static char const *strings[] = {
      "break",  "case", "continue", "defer", "else", "false", "if",    "in", "null",
      "return", "then", "true",     "try",   "void", "when",  "while", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

int is_reserved_type_keyword(char const *s) {
    static char const *strings[] = {
      "any",
      "void",
      null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;

    return 0;
}

int is_reserved_type_name(ast_node const *name) {
    // Check for reserved type keywords to disallow
    if (!ast_node_is_symbol(name)) return 0;
    str word = ast_node_str(name);
    if (is_reserved_type_keyword(str_cstr(&word))) return 1;
    return 0;
}

int is_name_already_defined(parser *self, str name) {
    // Only meaningful during the symbol-collection pass. In mode_source the module's
    // symbols are pre-loaded into current_module_symbols (see load_module_symbols),
    // so every declaration would look like a duplicate of itself.
    if (self->mode != mode_symbols) return 0;

    // Catches symbols from builtin.tl's top-level `#module builtin` section
    // (Int, UInt, Float, Option, Result, sizeof, ...).
    if (str_hset_contains(self->builtin_module_symbols, name)) return 1;

    // Catches primitive types registered directly in the type registry.
    if (tl_type_registry_get(self->opts.registry, name)) return 1;

    // Reject variant names that shadow an existing module name.
    // Example: `| String { ... }` in any module shadows `#module String`, making
    // `value: String` field references inside the variant's payload unresolvable.
    //
    // Exempt the canonical `#module Foo` + `Foo: | ...` pattern where the tagged
    // union's own name matches the enclosing module — that's idiomatic Tess.
    if (str_hset_contains(self->modules_seen, name) && !str_eq(name, self->current_module))
        return 1;

    // Catches duplicates within the current module.
    if (str_hset_contains(self->current_module_symbols, name)) return 1;

    return 0;
}

int is_index_operator(char const *s) {
    return 0 == strcmp(s, ".[");
}

int is_dot_operator(char const *s) {
    return 0 == strcmp(s, ".");
}

int is_struct_access_operator(char const *s) {
    static char const *strings[] = {".", "->", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_unary_operator(char const *s) {
    static char const *strings[] = {"!", "~", "+", "-", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_eof(parser *p) {
    return p->error.tag == tl_err_eof || p->tokenizer_error.tag == tl_err_eof;
}

int eat_comments(parser *p) {
    while (1) {
        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            parser_dbg(p, "tokenizer error: %s", tl_error_tag_to_string(p->tokenizer_error.tag));
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.col  = p->tokenizer_error.col;
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        token_tag const tag = p->token.tag;
        if (tok_comment == tag) {
            continue;
        } else {
            tokenizer_put_back(p->tokenizer, &p->token, 1);
            return 0;
        }
    }
}

int next_token(parser *p) {
    if (eat_comments(p)) return 1;

    while (1) {

        if (tokenizer_next(p->tokenizer, &p->token, &p->tokenizer_error)) {
            p->error.file = p->tokenizer_error.file;
            p->error.line = p->tokenizer_error.line;
            p->error.col  = p->tokenizer_error.col;
            p->error.tag  = tl_err_tokenizer_error;
            return 1;
        }

        // always update file/line
        p->error.file = p->token.file;
        p->error.line = p->token.line;
        p->error.col  = p->token.col;

        if (tok_comment == p->token.tag) continue;

        tokens_push_back(p, &p->token);
        return 0;
    }
}

nodiscard int a_peek(parser *p, parse_fun fun) {
    int                   result     = 0;
    u32 const             save_toks  = p->tokens.size;
    arena_watermark const save_arena = arena_save(p->ast_arena);

    result                           = fun(p);
    assert(p->tokens.size >= save_toks);
    if (p->tokens.size > save_toks) {
        tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
        tokens_shrink(p, save_toks);
    }
    arena_restore(p->ast_arena, save_arena);

    return result;
}

nodiscard int a_try(parser *p, parse_fun fun) {
    int                   result     = 0;
    u32 const             save_toks  = p->tokens.size;
    arena_watermark const save_arena = arena_save(p->ast_arena);

    if ((result = fun(p))) {
        assert(p->tokens.size >= save_toks);
        if (p->tokens.size > save_toks) {
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        arena_restore(p->ast_arena, save_arena);
        goto cleanup;
    }

cleanup:
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return result;
}

nodiscard int a_try_s(parser *p, parse_fun_s fun, char const *arg) {
    int                   result     = 0;
    u32 const             save_toks  = p->tokens.size;
    arena_watermark const save_arena = arena_save(p->ast_arena);
    if ((result = fun(p, arg))) {
        if (p->tokens.size > save_toks) {
            if (0) {
                char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
                alloc_free(p->transient, str);
            }
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        arena_restore(p->ast_arena, save_arena);
        return result;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

nodiscard int a_try_int(parser *p, parse_fun_int fun, int arg) {
    int                   result     = 0;
    u32 const             save_toks  = p->tokens.size;
    arena_watermark const save_arena = arena_save(p->ast_arena);
    if ((result = fun(p, arg))) {
        if (p->tokens.size > save_toks) {
            if (0) {
                char *str = token_to_string(p->transient, &p->tokens.v[save_toks]);
                alloc_free(p->transient, str);
            }
            tokenizer_put_back(p->tokenizer, &p->tokens.v[save_toks], p->tokens.size - save_toks);
            tokens_shrink(p, save_toks);
        }
        arena_restore(p->ast_arena, save_arena);
        return result;
    }
    // do not reset tokens on success, because calls to a_try may be
    // nested.
    return 0;
}

// primitives

int a_char(parser *p) {
    if (next_token(p)) return 1;
    if (tok_char == p->token.tag) return result_ast_str(p, ast_char, p->token.s);
    p->error.tag = tl_err_expected_comma;
    return 1;
}

int a_comma(parser *p) {
    if (next_token(p)) return 1;
    if (tok_comma == p->token.tag) return result_ast_str(p, ast_symbol, ",");
    p->error.tag = tl_err_expected_comma;
    return 1;
}

int a_dot(parser *p) {
    if (next_token(p)) return 1;
    if (tok_dot == p->token.tag) return result_ast_str(p, ast_symbol, ".");
    p->error.tag = tl_err_expected_dot;
    return 1;
}

int a_ellipsis(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ellipsis == p->token.tag) return result_ast_str(p, ast_symbol, "...");
    p->error.tag = tl_err_expected_ellipsis;
    return 1;
}

int a_vertical_bar(parser *p) {
    if (next_token(p)) return 1;
    if (tok_bar == p->token.tag) return result_ast_str(p, ast_symbol, "|");
    p->error.tag = tl_err_expected_vertical_bar;
    return 1;
}

int a_hash_command(parser *p) {
    if (next_token(p)) return 1;
    if (tok_hash_command == p->token.tag) {
        int res                            = result_ast_str(p, ast_hash_command, p->token.s);
        p->result->hash_command.words      = (str_sized){0};
        p->result->hash_command.is_c_block = 0;
        return res;
    }
    p->error.tag = tl_err_expected_hash_command;
    return 1;
}

int a_c_block(parser *p) {
    if (next_token(p)) return 1;
    if (tok_c_block == p->token.tag) {
        int res                            = result_ast_str(p, ast_hash_command, p->token.s);
        p->result->hash_command.words      = (str_sized){0};
        p->result->hash_command.is_c_block = 1;
        return res;
    }
    p->error.tag = tl_err_expected_hash_command;
    return 1;
}

int a_open_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_round == p->token.tag) return result_ast_str(p, ast_symbol, "(");
    p->error.tag = tl_err_expected_open_round;
    return 1;
}

int a_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[");
    p->error.tag = tl_err_expected_open_square;
    return 1;
}

int a_close_round(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_round == p->token.tag) return result_ast_str(p, ast_symbol, ")");
    p->error.tag = tl_err_expected_close_round;
    return 1;
}

int a_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]");
    p->error.tag = tl_err_expected_close_square;
    return 1;
}

int a_open_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_open_curly == p->token.tag) return result_ast_str(p, ast_symbol, "{");
    p->error.tag = tl_err_expected_open_curly;
    return 1;
}

int a_close_curly(parser *p) {
    if (next_token(p)) return 1;
    if (tok_close_curly == p->token.tag) return result_ast_str(p, ast_symbol, "}");
    p->error.tag = tl_err_expected_close_curly;
    return 1;
}

int        unmangle_arity(str name);
static str unmangle_arity_qualified_name(allocator *alloc, str name);

int        identifier_base(parser *p, str *name) {
    if (next_token(p)) return 1;

    if (tok_symbol != p->token.tag || 0 == strlen(p->token.s) || is_reserved(p->token.s)) goto error;

    char const c = p->token.s[0];
    if (('_' == c) || ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z')) {

        // first character good, check remaining characters
        char const *pc = &p->token.s[1];
        while (*pc) {
            if ('_' == *pc || ('a' <= *pc && *pc <= 'z') || ('A' <= *pc && *pc <= 'Z') ||
                ('0' <= *pc && *pc <= '9') || '/' == *pc) {
                pc++; // still good
            } else {
                goto error;
            }
        }

        // reject identifiers containing __ to avoid collisions with mangled names
        // exceptions: __init (compiler-recognized) and c__* (C interop prefix)
        if (str_contains(str_init_static(p->token.s), S("__")) && 0 != strcmp(p->token.s, "__init") &&
            0 != strncmp(p->token.s, "c__", 3)) {
            p->error.tag = tl_err_double_underscore_in_identifier;
            return ERROR_STOP;
        }

        // check for reserved words, which are not allowed as identifiers
        if (is_reserved(p->token.s)) goto error;

        *name = str_init(p->ast_arena, p->token.s);
        goto success;
    }

error:
    p->error.tag = tl_err_expected_identifier;
    return 1;

success:
    return 0;
}

int a_identifier(parser *p) {
    int res = 0;
    str name;
    if ((res = identifier_base(p, &name))) return res;

    result_ast_str_(p, ast_symbol, name);
    return 0;
}

int a_identifier_optional_arity(parser *p) {
    int res = 0;
    str name;
    if ((res = identifier_base(p, &name))) return res;

    // check for arity-qualified name
    int arity = unmangle_arity(name);
    if (arity != -1) {
        str base = unmangle_arity_qualified_name(p->ast_arena, name);
        assert(!str_is_empty(base));

        // Resolve function alias: alias_name/2 → Module__func__2
        if (p->function_aliases) {
            function_alias_info *alias = str_map_get_ptr(p->function_aliases, base);
            if (alias) {
                str mangled = mangle_str_for_arity(p->ast_arena, alias->base_name, (u8)arity);
                mangled     = mangle_str_for_module(p, mangled, alias->module);
                result_ast_str_(p, ast_symbol, mangled);
                p->result->symbol.is_module_mangled = 1;
                p->result->symbol.module            = str_copy(p->ast_arena, alias->module);
                return 0;
            }
        }

        str mangled = mangle_str_for_arity(p->ast_arena, base, (u8)arity);
        return result_ast_str_(p, ast_symbol, mangled);
    }

    return result_ast_str_(p, ast_symbol, name);
}

int a_attributed_identifier(parser *self) {
    // All identifiers may now have an attribute set, denoted by a [[...]] expression immediately preceding
    // the identifer. This will be tokenized as `[[, <zero or more tokens>, ]]`

    // This is pathed through a_value, so it must also support arity-qualified identifiers, e.g. foo/1

    ast_node *attributes = null;
    if (0 == a_try(self, a_attribute_set)) {
        attributes = self->result;
    }
    if (0 == a_try(self, a_identifier_optional_arity)) {
        ast_node_set_attributes(self->result, attributes);
        return 0;
    }
    return 1;
}

int the_symbol(parser *p, char const *const want) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp(want, p->token.s)) return result_ast_str(p, ast_symbol, p->token.s);
    }

    p->error.tag = tl_err_expected_specific_symbol;
    return 1;
}

// Create a String.from_literal(s) NFA node.
static ast_node *make_from_literal(parser *self, char const *s) {
    ast_node_sized args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(void *))};
    args.v[0]           = ast_node_create_sym_c(self->ast_arena, s);
    args.v[0]->tag      = ast_string;
    set_node_file(self, args.v[0]);

    ast_node *name = ast_node_create_sym_c(self->ast_arena, "from_literal__1");
    mangle_name_for_module(self, name, S("String"));
    return ast_node_create_nfa(self->ast_arena, name, (ast_node_sized){0}, args);
}

// Parse a format spec string: [[fill]align][sign][#][0][width][.precision][type]
// Returns 0 on success, 1 on parse error.
static int parse_format_spec(char const *s, tl_format_spec *out) {
    *out          = (tl_format_spec){.precision = -1};

    char const *p = s;
    if (!*p) return 0; // empty spec is valid (no-op)

    // [[fill]align] — if char after first is an align char, first is fill
    if (p[0] && p[1] && (p[1] == '<' || p[1] == '>' || p[1] == '^')) {
        out->fill  = p[0];
        out->align = p[1];
        p += 2;
    } else if (*p == '<' || *p == '>' || *p == '^') {
        out->align = *p++;
    }

    // [sign]
    if (*p == '+' || *p == '-' || *p == ' ') {
        out->sign              = *p++;
        out->has_type_specific = 1;
    }

    // [#]
    if (*p == '#') {
        out->alt               = 1;
        out->has_type_specific = 1;
        p++;
    }

    // [0][width] — a leading '0' means zero-pad, followed by optional width digits
    if (*p == '0') {
        out->zero_pad          = 1;
        out->has_type_specific = 1;
        p++;
    }
    if (*p >= '1' && *p <= '9') {
        out->width = 0;
        while (*p >= '0' && *p <= '9') out->width = out->width * 10 + (*p++ - '0');
    }

    // [.precision]
    if (*p == '.') {
        p++;
        out->precision         = 0;
        out->has_type_specific = 1;
        while (*p >= '0' && *p <= '9') out->precision = out->precision * 10 + (*p++ - '0');
    }

    // [type]
    if (*p == 'd' || *p == 'x' || *p == 'X' || *p == 'o' || *p == 'b' || *p == 'e' || *p == 'E' ||
        *p == 'f') {
        out->type_char         = *p++;
        out->has_type_specific = 1;
    }

    // Must have consumed everything
    return *p != '\0';
}

int a_string(parser *self) {
    if (next_token(self)) return 1;

    if (tok_c_string == self->token.tag) return result_ast_str(self, ast_string, self->token.s);

    if (tok_string == self->token.tag || tok_s_string == self->token.tag) {
        ast_node *r = make_from_literal(self, self->token.s);
        return result_ast_node(self, r);
    }

    if (tok_f_string_start == self->token.tag) {
        // Committed parse — no backtracking after this point.
        // Collect interleaved literal parts and expressions.
        ast_node_array parts = {.alloc = self->ast_arena};

        // Track per-part format specs. Index-parallel with parts.
        tl_format_spec_array specs     = {.alloc = self->ast_arena};
        tl_format_spec const zero_spec = {0};

        // Add first literal segment (if non-empty)
        if (self->token.s[0] != '\0') {
            ast_node *lit = make_from_literal(self, self->token.s);
            array_push(parts, lit);
            array_push(specs, zero_spec); // zero-init slot for literal (no spec)
        }

        int has_any_spec = 0;

        while (1) {
            // Parse the expression inside { }
            ast_node *expr = parse_expression(self, INT_MIN);
            if (!expr) {
                self->error.tag = tl_err_expected_expression;
                return ERROR_STOP;
            }
            array_push(parts, expr);

            // Next token: format spec, mid, or end
            if (next_token(self)) return ERROR_STOP;

            // Optional format specifier
            if (tok_f_string_format_spec == self->token.tag) {
                tl_format_spec spec;
                if (parse_format_spec(self->token.s, &spec)) {
                    self->error.tag = tl_err_invalid_format_spec;
                    return ERROR_STOP;
                }
                array_push(specs, spec);
                has_any_spec = 1;

                if (next_token(self)) return ERROR_STOP;
            } else {
                array_push(specs, zero_spec);
            }

            if (tok_f_string_mid == self->token.tag) {
                if (self->token.s[0] != '\0') {
                    ast_node *mid = make_from_literal(self, self->token.s);
                    array_push(parts, mid);
                    array_push(specs, zero_spec); // zero-init slot for literal
                }
                continue;
            }
            if (tok_f_string_end == self->token.tag) {
                if (self->token.s[0] != '\0') {
                    ast_node *end = make_from_literal(self, self->token.s);
                    array_push(parts, end);
                    array_push(specs, zero_spec); // zero-init slot for literal
                }
                break;
            }

            // Unexpected token
            self->error.tag = tl_err_expected_expression;
            return ERROR_STOP;
        }

        // Construct Print.format(...parts) NFA
        ast_node *name = ast_node_create_sym_c(self->ast_arena, "format__1");
        mangle_name_for_module(self, name, S("Print"));
        ast_node_sized args = {.size = parts.size, .v = parts.v};
        ast_node      *r    = ast_node_create_nfa(self->ast_arena, name, (ast_node_sized){0}, args);
        r->named_application.is_variadic_call = 1;
        r->named_application.n_fixed_args     = 0;

        // Attach format specs if any were present
        if (has_any_spec && specs.size) {
            array_shrink(specs);
            tl_fstring_format *ffmt          = alloc_malloc(self->ast_arena, sizeof(tl_fstring_format));
            ffmt->specs                      = specs.v;
            ffmt->uses_format                = null;
            ffmt->layout_fn                  = str_empty();
            r->named_application.fstring_fmt = ffmt;
        }

        return result_ast_node(self, r);
    }

    self->error.tag = tl_err_expected_string;
    return 1;
}

int string_to_number(parser *parser, char const *const in) {
    i64 i;
    u64 u;
    f64 d;
    switch (str_parse_cnum(in, &i, &u, &d)) {
    case 1:  return result_ast_i64(parser, i);
    case 2:  return result_ast_u64(parser, u);
    case 3:  return result_ast_f64(parser, d);
    case 4:  return result_ast_i64_z(parser, i);
    case 5:  return result_ast_u64_zu(parser, u);
    default: return 1;
    }
}

int a_number(parser *self) {
    if (next_token(self)) return 1;

    if (tok_number == self->token.tag) {

        if (string_to_number(self, self->token.s)) goto error;
        // sets parser result

        return 0;
    }

error:
    self->error.tag = tl_err_expected_number;
    return 1;
}

int a_bool(parser *p) {
    if (next_token(p)) return 1;

    if (tok_symbol == p->token.tag) {
        if (0 == strcmp("true", p->token.s)) return result_ast_bool(p, 1);
        if (0 == strcmp("false", p->token.s)) return result_ast_bool(p, 0);
    }

    p->error.tag = tl_err_expected_bool;
    return 1;
}

int a_equal_sign(parser *p) {
    if (next_token(p)) return 1;

    if (tok_equal_sign == p->token.tag) return result_ast_str(p, ast_symbol, "=");

    p->error.tag = tl_err_expected_equal_sign;
    return 1;
}

int a_colon(parser *p) {
    if (next_token(p)) return 1;
    if (tok_colon == p->token.tag) return result_ast_str(p, ast_symbol, ":");
    p->error.tag = tl_err_expected_colon;
    return 1;
}

int a_semicolon(parser *p) {
    if (next_token(p)) return 1;
    if (tok_semicolon == p->token.tag) return result_ast_str(p, ast_symbol, ";");
    p->error.tag = tl_err_expected_semicolon;
    return 1;
}

int a_double_open_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_open_square == p->token.tag) return result_ast_str(p, ast_symbol, "[[");
    p->error.tag = tl_err_expected_double_open_square;
    return 1;
}

int a_double_close_square(parser *p) {
    if (next_token(p)) return 1;
    if (tok_double_close_square == p->token.tag) return result_ast_str(p, ast_symbol, "]]");
    p->error.tag = tl_err_expected_double_close_square;
    return 1;
}

int a_star(parser *p) {
    if (next_token(p)) return 1;
    if (tok_star == p->token.tag) return result_ast_str(p, ast_symbol, "*");
    p->error.tag = tl_err_expected_star;
    return 1;
}

int is_ampersand(ast_node const *node) {
    return (ast_node_is_symbol(node) && str_eq(ast_node_str(node), S("&")));
}

int a_ampersand(parser *p) {
    if (next_token(p)) return 1;
    if (tok_ampersand == p->token.tag) return result_ast_str(p, ast_symbol, "&");
    p->error.tag = tl_err_expected_ampersand;
    return 1;
}

int a_colon_equal(parser *p) {
    if (next_token(p)) return 1;

    if (tok_colon_equal == p->token.tag) return result_ast_str(p, ast_symbol, ":=");

    p->error.tag = tl_err_expected_colon_equal;
    return 1;
}

int a_arrow(parser *p) {
    if (next_token(p)) return 1;

    if (tok_arrow == p->token.tag) return result_ast_str(p, ast_symbol, "->");

    p->error.tag = tl_err_expected_arrow;
    return 1;
}

int a_nil(parser *self) {

    if (0 == a_try_s(self, the_symbol, "void")) return result_ast(self, ast_void);
    if ((0 == a_open_round(self)) && (0 == a_close_round(self))) return result_ast(self, ast_void);

    self->error.tag = tl_err_expected_nil;
    return 1;
}

int a_null(parser *self) {
    if (0 == the_symbol(self, "null")) return result_ast(self, ast_nil);
    self->error.tag = tl_err_expected_nil;
    return 1;
}

int set_node_parameters(parser *self, ast_node *node, ast_node_array *parameters) {
    // given parsed parameters for a function or lambda, initialize
    // the node array properly

    array_shrink(*parameters);
    node->array.nodes = parameters->v;
    if (parameters->size > 0xff) return too_many_arguments(self);

    node->array.n = (u8)parameters->size;
    return 0;
}

int a_type_identifier_base(parser *self);

int maybe_mangle_binop(parser *self, ast_node *op, ast_node **inout, ast_node *right);

int a_type_identifier_base(parser *self) {
    // Callers expect name to be mangled.
    if (0 == a_try(self, a_type_arrow)) return 0;

    if (0 == a_try(self, a_attributed_identifier)) {
        ast_node *ident = self->result;

        // Iteratively resolve dotted names left-to-right (e.g., CommandLine.Args.Args).
        // This mirrors how parse_expression handles module-qualified names via
        // maybe_mangle_binop in a while loop.
        while (0 == a_try(self, a_dot)) {
            ast_node *op = self->result;

            if (a_try(self, a_attributed_identifier)) return 1;
            ast_node *right = self->result;

            if (maybe_mangle_binop(self, op, &ident, right)) {
                continue; // combined module or resolved member — check for more dots
            } else {
                mangle_name(self, right);
                return result_ast_node(self, right);
            }
        }

        // No (more) dots — parse optional type arguments.
        ast_node_array type_args;
        if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

        mangle_name(self, ident);

        maybe_mangle_implicit_submodule(self, ident);

        if (type_args.size) {
            ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                              (ast_node_sized){0});
            return result_ast_node(self, r);
        } else {
            return result_ast_node(self, ident);
        }
    }

    return 1;
}

int a_type_identifier(parser *self) {
    if (0 == a_try(self, a_ellipsis)) {
        // Try to parse a following type identifier: ...TraitName
        // If found, create NFA(name="...", type_args=[TraitName]) for variadic type.
        // If not found, return bare "..." (C FFI ellipsis).
        if (0 == a_try(self, a_type_identifier_base)) {
            ast_node *trait_name = self->result;
            ast_node *dots       = ast_node_create_sym_c(self->ast_arena, "...");
            set_node_file(self, dots);
            ast_node *r = ast_node_create_nfa(
              self->ast_arena, dots, (ast_node_sized){.v = &trait_name, .size = 1}, (ast_node_sized){0});
            // Clone the type_argument since it was on stack
            r->named_application.type_arguments    = alloc_malloc(self->ast_arena, sizeof(ast_node *));
            r->named_application.type_arguments[0] = trait_name;
            return result_ast_node(self, r);
        }
        return 0;
    }
    return a_type_identifier_base(self);
}

int a_type_annotation(parser *self) {
    if (0 == a_try(self, a_colon)) {
        int res = a_try(self, a_type_identifier);
        return res;
    }

    self->error.tag = tl_err_expected_type;
    return 1;
}

int a_param(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *ident = self->result;

    // If followed by type arguments (e.g., Ptr[K] in a function pointer type),
    // parse as a generic type application rather than a named parameter.
    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (type_args.size) {
        mangle_name(self, ident);
        ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                          (ast_node_sized){0});
        return result_ast_node(self, r);
    }

    ast_node *ann = null;
    if (0 == a_try(self, a_type_annotation)) {
        ann = self->result;
    }

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;
    return result_ast_node(self, ident);
}

int unmangle_arity(str name) {
    // Returns -1 if name is not mangled with the format `foo/X` where X is an integer in the range [0,
    // 255]. Otherwise, returns X.
    char const *s = str_cstr(&name);
    char const *p = strrchr(s, '/');
    if (!p || p == s) return -1;
    if ((size_t)(p - s) + 1 >= str_len(name)) return -1;
    p++;
    // check for '0' directly because strtol returns 0 upon error
    if (*p == '0') return 0;

    long num = strtol(p, null, 10);
    if (num > 255 || num < 1) return -1;
    return (int)num;
}

static str unmangle_arity_qualified_name(allocator *alloc, str name) {
    // Returns str_empty if name is not arity-qualified (e.g. `foo/X`). Otherwise returns the name portion
    // as a newly allocated str.
    char const *s = str_cstr(&name);
    char const *p = strrchr(s, '/');
    if (!p || p == s) return str_empty();
    if ((size_t)(p - s) + 1 >= str_len(name)) return str_empty();
    return str_init_n(alloc, s, p - s);
}

void unmangle_name(parser *self, ast_node *name) {
    (void)self;
    if (ast_node_is_nfa(name)) {
        unmangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (!name->symbol.is_module_mangled) return;
    if (str_is_empty(name->symbol.original)) return;
    name->symbol.name              = name->symbol.original;
    name->symbol.is_module_mangled = 0;
    str_deinit(self->ast_arena, &name->symbol.module);
}

// Check if a symbol is a known module-level function (not a type).
// Returns true only for symbols that exist in module_symbols (with arity mangling) and are not types.
// Used for arity mangling: only mangle calls to known module functions, not local variables.
int symbol_is_module_function(parser *self, ast_node *name, u8 arity) {
    if (!ast_node_is_symbol(name)) return 0;

    // Get the original (unmangled) name for type check
    str original_name = name->symbol.original;
    if (str_is_empty(original_name)) original_name = name->symbol.name;

    // Types should not be mangled for arity
    if (tl_type_registry_get(self->opts.registry, original_name)) return 0;

    // Generate the arity-mangled name to search for
    str mangled_name = mangle_str_for_arity(self->ast_arena, original_name, arity);

    // Check builtin symbols first (e.g., sizeof, alignof)
    if (str_hset_contains(self->builtin_module_symbols, mangled_name)) return 1;

    // Get module name
    str module_name = name->symbol.module;

    // For symbols in the current module (empty module name), check current_module_symbols directly.
    // This handles the case where we're parsing a call within the same module where the function is
    // defined.
    if (str_is_empty(module_name)) {
        int found = str_hset_contains(self->current_module_symbols, mangled_name);
        return found;
    }

    // For cross-module references, look up the module's symbol table
    hashmap *module_syms = resolve_module_symbols(self, module_name);
    if (!module_syms) return 0;

    // Check if arity-mangled symbol exists in that module
    return str_hset_contains(module_syms, mangled_name);
}

str mangle_str_for_module(parser *self, str name, str module) {
    str safe_module = str_replace_char_str(self->ast_arena, module, '.', S("__"));
    str result      = str_qualify(self->ast_arena, safe_module, name);

    if (self->module_pkg_prefixes) {
        str *prefix = str_map_get(self->module_pkg_prefixes, module);
        if (prefix) {
            result = str_qualify(self->ast_arena, *prefix, result);
        }
    }
    return result;
}

str mangle_str_for_arity(allocator *alloc, str name, u8 arity) {
    return str_fmt(alloc, "%s__%i", str_cstr(&name), (int)arity);
}

void mangle_name_for_module(parser *self, ast_node *name, str module) {
    if (ast_node_is_symbol(name) && !str_is_empty(module)) {
        ast_node_name_replace(name, mangle_str_for_module(self, name->symbol.name, module));
        name->symbol.is_module_mangled = 1;
        name->symbol.module            = str_copy(self->ast_arena, module);
    }
}

// Implicit submodule resolution: bare "Child" in #module Parent resolves to
// Parent__Child__Child if Parent.Child is a submodule with a Child symbol.
void maybe_mangle_implicit_submodule(parser *self, ast_node *name) {
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_module_mangled) return;
    if (str_is_empty(self->current_module)) return;

    str name_str = ast_node_str(name);

    // Top-level modules take precedence over implicit submodule resolution.
    if (str_hset_contains(self->modules_seen, name_str)) return;

    str sub_module = str_cat_3(self->transient, self->current_module, S("."), name_str);
    if (!str_hset_contains(self->modules_seen, sub_module)) return;

    hashmap *sub_syms = resolve_module_symbols(self, sub_module);
    if (sub_syms && str_hset_contains(sub_syms, name_str)) {
        mangle_name_for_module(self, name, sub_module);
    }
}

void mangle_name_for_arity(parser *self, ast_node *name, u8 arity, int is_definition) {
    if (!ast_node_is_symbol(name)) return;
    str name_str = name->symbol.name;
    if (is_main_function(name_str)) return;
    if (is_c_symbol(name_str)) return;

    // For function calls (not definitions), only mangle if the symbol is a known module-level function.
    // Do NOT mangle unknown symbols - they might be local variables (parameters, let bindings).
    // This is the key fix: previously we mangled unknown symbols, which broke calls like f(x)
    // where f is a parameter.
    // For function definitions, always mangle - the definition establishes the mangled name.
    if (!is_definition && !symbol_is_module_function(self, name, arity)) {
        return;
    }

    ast_node_name_replace(name, mangle_str_for_arity(self->ast_arena, name_str, arity));
    name_str = name->symbol.name;

    // Don't set is_mangled: that flag is to indicate module mangling, not arity mangling.
    parser_dbg(self, "arity mangle '%s' to '%s'\n", str_cstr(&name->symbol.original), str_cstr(&name_str));
}

void mangle_name(parser *self, ast_node *name) {
    // Note: module `main` set current_module to empty, so names are not mangled at all.
    if (str_is_empty(self->current_module)) return;
    if (ast_node_is_nfa(name)) {
        mangle_name(self, name->named_application.name);
        return;
    }
    if (!ast_node_is_symbol(name)) return;
    if (name->symbol.is_module_mangled) return;
    if (is_intrinsic(name->symbol.name)) return;

    // Don't mangle names of known types
    str name_str = ast_node_str(name);
    if (tl_type_registry_get(self->opts.registry, name_str)) return;

    // Don't mangle c_ names
    if (is_c_symbol(name_str)) return;

    // Don't mangle names in 'builtin' module
    if (str_eq(self->current_module, S("builtin"))) return;
    if (str_hset_contains(self->builtin_module_symbols, name_str)) return;

    // Don't mangle names that haven't been defined in the current module
    if (!str_hset_contains(self->current_module_symbols, name_str)) return;

    mangle_name_for_module(self, name, self->current_module);
}

ast_node *parse_lvalue(parser *self) {
    ast_node *ident = null;

    // Fast bail: if the next token cannot start an lvalue, skip the expensive
    // parse_expression call. This prevents exponential backtracking when
    // a_assignment/a_reassignment speculatively parse complex expressions
    // (e.g. nested if/else) that can never be lvalues.
    if (a_peek(self, next_token)) return null;
    token_tag tag              = self->token.tag;
    int       cannot_be_lvalue = (tag == tok_symbol) ? is_reserved(self->token.s)
                                                     : (tag != tok_open_round && tag != tok_double_open_square);
    if (cannot_be_lvalue) return null;

    ast_node *expr = parse_expression(self, INT_MIN);
    if (!expr) return null;

    if (ast_node_is_symbol(expr)) ident = expr;
    else if (ast_binary_op == expr->tag) {
        char const *op = str_cstr(&expr->binary_op.op->symbol.name);
        if (is_struct_access_operator(op) || is_index_operator(op)) ident = expr;
    } else if (ast_unary_op == expr->tag) {
        if (str_eq(ast_node_str(expr->unary_op.op), S("*"))) ident = expr;
    }
    if (!ident) return null;

    ast_node *ann = null;
    if (0 == a_try(self, a_type_annotation)) {
        ann                = self->result;

        ast_node *leftmost = ident;
        while (!ast_node_is_symbol(leftmost)) {
            if (ast_binary_op == leftmost->tag) leftmost = leftmost->binary_op.left;
            else fatal("logic error");
        }

        leftmost->symbol.annotation = ann;
    }

    return ident;
}

int a_field_assignment(parser *self) {
    // x = val (for type literals)
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *name = self->result;

    ast_node *ann  = null;
    if (0 == a_try(self, a_type_annotation)) ann = self->result;

    if (a_try(self, a_equal_sign)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a                           = ast_node_create_assignment(self->ast_arena, name, val);
    a->assignment.is_field_name           = 1;
    a->assignment.name->symbol.annotation = ann;
    return result_ast_node(self, a);
}

// Parse an optional trait bound on a type parameter: T: Trait
// Expects self->result to be the type parameter identifier.
// On success, self->result is still the type parameter (with annotation set if bound present).
int maybe_trait_bound(parser *self) {
    ast_node *type_param = self->result;
    if (0 == a_try(self, a_colon)) {
        if (a_try(self, a_type_identifier)) return 1;
        type_param->symbol.annotation = self->result;
    }
    self->result = type_param;
    return 0;
}

int maybe_type_parameters(parser *self, ast_node_array *out) {
    *out = (ast_node_array){.alloc = self->ast_arena};
    if (0 == a_try(self, a_open_square)) {
        if (0 == a_try(self, a_close_square)) return 0;
        if (a_try(self, a_identifier)) return 1;
        if (maybe_trait_bound(self)) return 1;
        array_push(*out, self->result);

        while (1) {
            if (0 == a_try(self, a_close_square)) return 0;
            if (a_try(self, a_comma)) return 1;
            if (a_try(self, a_identifier)) return 1;
            if (maybe_trait_bound(self)) return 1;
            array_push(*out, self->result);
        }
    }
    return 0;
}

// Check if an annotation AST node is a variadic type: NFA(name="...", type_args=[TraitName])
static int is_variadic_annotation(ast_node *ann) {
    if (!ann) return 0;
    if (!ast_node_is_nfa(ann)) return 0;
    if (!ast_node_is_symbol(ann->named_application.name)) return 0;
    return str_eq(ann->named_application.name->symbol.name, S("..."));
}

// Extract the trait name from a variadic type annotation
static str variadic_trait_name(ast_node *ann) {
    // ann is NFA(name="...", type_args=[TraitName])
    assert(ann->named_application.n_type_arguments == 1);
    ast_node *trait = ann->named_application.type_arguments[0];
    if (ast_node_is_symbol(trait)) return trait->symbol.name;
    if (ast_node_is_nfa(trait)) return ast_node_str(trait->named_application.name);
    return str_empty();
}

// Register a variadic function in the variadic_symbols map.
// base_name: unmangled function name (e.g. "print")
// mangled: arity-mangled name (e.g. "print__2")
// n_fixed: number of fixed (non-variadic) params
// trait: trait name from the variadic bound
// module: current module name
static void register_variadic_symbol(parser *self, str base_name, str mangled, u8 n_fixed, str trait,
                                     str module) {
    variadic_symbol_info *info = alloc_malloc(self->parent_alloc, sizeof(variadic_symbol_info));
    info->n_fixed_params       = n_fixed;
    info->mangled_name         = str_copy(self->parent_alloc, mangled);
    info->trait_name           = str_copy(self->parent_alloc, trait);
    info->module               = str_copy(self->parent_alloc, module);
    str_map_set_ptr(&self->variadic_symbols, base_name, info);
}

// Detect whether the last parameter has a variadic annotation (...TraitName),
// compute arity, mangle the name, and register the variadic symbol if found.
// Returns 1 if variadic, 0 otherwise. *out_arity receives the mangled arity.
int detect_and_register_variadic(parser *self, ast_node *name, ast_node_sized params, u8 *out_arity) {
    int is_variadic = 0;
    u8  n_fixed     = (u8)params.size;

    if (params.size > 0) {
        ast_node *last_param = params.v[params.size - 1];
        if (ast_node_is_symbol(last_param) && is_variadic_annotation(last_param->symbol.annotation)) {
            is_variadic = 1;
            n_fixed     = (u8)(params.size - 1);
        }
    }

    *out_arity = is_variadic ? (u8)(n_fixed + 1) : (u8)params.size;
    mangle_name_for_arity(self, name, *out_arity, 1);
    add_module_symbol(self, name);

    if (is_variadic) {
        ast_node *last_param = params.v[params.size - 1];
        str       trait      = variadic_trait_name(last_param->symbol.annotation);
        str       base_name  = name->symbol.original;
        register_variadic_symbol(self, base_name, name->symbol.name, n_fixed, trait, self->current_module);
    }

    return is_variadic;
}

int toplevel_defun(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;
    ast_node_array type_params;
    ast_node_array params = {.alloc = self->ast_arena};

    if (maybe_type_parameters(self, &type_params)) return 1;
    int res = parse_param_list(self, &params, 1);
    if (res) return res;

    // optional arrow: if it is present, we need to add an annotation to the defun name's symbol
    if (0 == a_try(self, a_arrow)) {

        if (a_try(self, a_type_identifier)) return 1;
        ast_node *ann   = self->result;

        ast_node *arrow = parser_make_arrow(self, params, ann, (ast_node_sized)sized_all(type_params));

        // attach to name
        name->symbol.annotation = arrow;
    }

    if (a_try(self, a_open_curly)) return 1;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};

    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return ERROR_STOP; // stop parsing
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs, defers);

    u8        arity;
    int is_variadic = detect_and_register_variadic(self, name, (ast_node_sized)array_sized(params), &arity);

    mangle_name(self, name);

    ast_node *let = ast_node_create_let(self->ast_arena, name, (ast_node_sized)sized_all(type_params),
                                        (ast_node_sized)sized_all(params), body);
    set_node_parameters(self, let, &params);
    let->let.name        = name;
    let->let.body        = body;
    let->let.is_variadic = is_variadic;

    result_ast_node(self, let);

    return 0;
}

// Check whether a module's symbol table contains any arity-mangled variant of `name`
// (e.g. "func__0", "func__1", ...). Functions always have arity-mangled entries; types don't.
static int module_has_function(parser *self, hashmap *mod_syms, str name) {
    for (u8 a = 0; a <= 32; a++) {
        str mangled = mangle_str_for_arity(self->transient, name, a);
        if (str_hset_contains(mod_syms, mangled)) return 1;
    }
    return 0;
}

int toplevel_function_alias(parser *self) {
    // Parses: name = Module.func
    // Registers the alias so call sites are rewritten to the target function.
    // No AST node is emitted — the alias is purely a parse-time name rewrite.
    //
    // Distinguished from type aliases by checking whether the target is a function
    // (has arity-mangled entries in the module's symbol table) or a variadic function.

    if (a_try(self, a_identifier)) return 1;
    ast_node *alias_name = self->result;
    if (!ast_node_is_symbol(alias_name)) return 1;

    str alias_str = ast_node_str(alias_name);

    if (a_try(self, a_equal_sign)) return 1;

    // RHS: Module.func
    if (a_try(self, a_identifier)) return 1;
    ast_node *module_node = self->result;
    if (!ast_node_is_symbol(module_node)) return 1;

    if (a_try(self, a_dot)) return 1;

    if (a_try(self, a_identifier)) return 1;
    ast_node *func_node = self->result;
    if (!ast_node_is_symbol(func_node)) return 1;

    str module_name = ast_node_str(module_node);
    str func_name   = ast_node_str(func_node);

    // Verify the target module exists (its symbols must have been collected in pass 1)
    hashmap *mod_syms = resolve_module_symbols(self, module_name);
    if (!mod_syms) return 1;

    // Verify the target is a function, not a type. Functions have arity-mangled entries
    // (e.g. "func__0") in the module's symbol table; types have bare names (e.g. "Allocator").
    // Also check variadic_symbols for variadic functions.
    int                   is_function = module_has_function(self, mod_syms, func_name);
    variadic_symbol_info *vinfo       = null;
    if (!is_function && self->variadic_symbols) {
        vinfo = str_map_get_ptr(self->variadic_symbols, func_name);
        if (vinfo && str_eq(vinfo->module, module_name)) is_function = 1;
        else vinfo = null;
    }
    if (!is_function) return 1;

    // Register the alias
    function_alias_info *info = alloc_malloc(self->parent_alloc, sizeof(function_alias_info));
    info->module              = str_copy(self->parent_alloc, module_name);
    info->base_name           = str_copy(self->parent_alloc, func_name);
    if (!self->function_aliases)
        self->function_aliases = map_new(self->parent_alloc, str, function_alias_info *, 16);
    str_map_set_ptr(&self->function_aliases, alias_str, info);

    // Propagate variadic info
    if (vinfo) {
        register_variadic_symbol(self, alias_str, vinfo->mangled_name, vinfo->n_fixed_params,
                                 vinfo->trait_name, vinfo->module);
    }

    parser_dbg(self, "function alias '%s' -> '%s.%s'\n", str_cstr(&alias_str), str_cstr(&module_name),
               str_cstr(&func_name));

    // Emit a dummy result node (required by the parser framework).
    // In mode_source (pass 2), toplevel() will skip this node.
    return result_ast_node(self, alias_name);
}

int toplevel_assign(parser *self) {
    // cannot use parse_lvalue here
    if (a_try(self, a_param)) return 1;
    ast_node *name = self->result;

    if (a_try(self, a_colon_equal)) return 1;
    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) return ERROR_STOP;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    ast_node *n = ast_node_create_let_in(self->ast_arena, name, value, null);

    add_module_symbol(self, name);
    mangle_name(self, name);
    return result_ast_node(self, n);
}

int toplevel_forward(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (maybe_type_parameters(self, &type_args)) return 1;

    if (a_try(self, a_type_arrow)) return 1;
    ast_node *arrow = self->result;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(name)) return ERROR_STOP;

    // add type arguments
    array_shrink(type_args);
    arrow->arrow.n_type_parameters = type_args.size;
    arrow->arrow.type_parameters   = type_args.v;

    // attach to name
    name->symbol.annotation = arrow;

    // Get arity from the arrow's parameter tuple
    ast_node_sized params = ast_node_sized_from_ast_array_const(arrow->arrow.left);

    u8             arity;
    detect_and_register_variadic(self, name, params, &arity);

    mangle_name(self, name);

    return result_ast_node(self, name);
}

int toplevel_symbol_annotation(parser *self) {
    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node *ident = self->result;

    if (a_try(self, a_type_annotation)) return ERROR_STOP;
    ast_node *ann = self->result;

    // Check for reserved type keywords to disallow
    if (is_reserved_type_name(ident)) return ERROR_STOP;

    assert(ast_node_is_symbol(ident));
    ident->symbol.annotation = ann;

    if (ast_node_is_arrow(ident->symbol.annotation)) {
        ast_node      *arrow  = ident->symbol.annotation;
        ast_node_sized params = ast_node_sized_from_ast_array_const(arrow->arrow.left);
        mangle_name_for_arity(self, ident, params.size, 1); // 1 = symbol annotation (definition)
    }

    add_module_symbol(self, ident);
    mangle_name(self, ident);
    return result_ast_node(self, ident);
}

int toplevel_c_chunk(parser *self) {
    if (a_try(self, a_c_block)) return 1;
    return 0;
}

// Build "prefix::module" versioned key for multi-version module isolation.
static str make_version_key(allocator *alloc, str prefix, str module_name) {
    return str_cat_3(alloc, prefix, S("::"), module_name);
}

hashmap *resolve_module_symbols(parser *self, str module_name) {
    if (self->module_pkg_prefixes) {
        str *pfx = str_map_get(self->module_pkg_prefixes, module_name);
        if (pfx) {
            // transient is fine: key is used for lookup only, map_set copies key bytes
            str      vkey = make_version_key(self->transient, *pfx, module_name);
            hashmap *syms = str_map_get_ptr(self->module_symbols, vkey);
            if (syms) return syms;
        }
    }
    return str_map_get_ptr(self->module_symbols, module_name);
}

void save_current_module_symbols(parser *self) {
    if (self->mode != mode_symbols) return;
    str      module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    hashmap *copy        = map_copy(self->current_module_symbols);

    // Use versioned key when a package prefix exists, bare name otherwise.
    // resolve_module_symbols() mirrors this: versioned lookup first, bare fallback.
    if (self->module_pkg_prefixes) {
        str *pfx = str_map_get(self->module_pkg_prefixes, module_name);
        if (pfx) {
            str vkey = make_version_key(self->ast_arena, *pfx, module_name);
            str_map_set_ptr(&self->module_symbols, vkey, copy);
            return;
        }
    }
    str_map_set_ptr(&self->module_symbols, module_name, copy);
}

void load_module_symbols(parser *self) {
    str      module_name = str_is_empty(self->current_module) ? S("main") : self->current_module;
    hashmap *syms        = resolve_module_symbols(self, module_name);
    if (syms) {
        // Don't destroy current_module_symbols here - after the first load, it points to a hashmap
        // in module_symbols, and destroying it would corrupt module_symbols entries.
        // The memory is managed by module_symbols and will be freed when the parser is destroyed.
        self->current_module_symbols = syms;
    }
}

void toplevel_hash_unity_file(parser *self, str argument) {
    self->skip_module   = 0;
    self->expect_module = 1;
    tokenizer_set_file(self->tokenizer, argument);
    map_reset(self->module_aliases);
}

int toplevel_hash_module(parser *self, str module) {
    // Modules can be re-opened: if #module Foo appears again after #module Foo.Bar, parsing
    // resumes in Foo with its previously collected symbols intact.
    self->skip_module   = 0;
    self->expect_module = 0;

    // reject module names containing __ to avoid collisions with mangled names
    if (str_contains(module, S("__"))) {
        self->error.tag = tl_err_double_underscore_in_identifier;
        return ERROR_STOP;
    }

    // Validate immediate parent module exists for nested modules (e.g., Foo.Bar must exist for
    // Foo.Bar.Baz)
    str parent = str_empty();
    if (str_rprefix_char(self->transient, module, '.', &parent)) {
        int parent_known = str_hset_contains(self->modules_seen, parent) ||
                           (self->opts.known_modules && str_map_contains(self->opts.known_modules, parent));
        if (!parent_known) {
            self->error.tag = tl_err_nested_module_parent_not_found;
            return ERROR_STOP;
        }
    }
    str_deinit(self->transient, &parent);

    // Version-aware dedup: when a package prefix is available, use "prefix::module" key in
    // modules_version_seen so different versions both get parsed while same-version diamond
    // deps are correctly skipped.  modules_seen keeps bare names only — all other consumers
    // (nested-parent validation, #use, #alias, etc.) are unaffected.
    int  already_seen = 0;
    str *pfx          = self->module_pkg_prefixes ? str_map_get(self->module_pkg_prefixes, module) : null;
    if (pfx) {
        str vkey     = make_version_key(self->transient, *pfx, module);
        already_seen = str_hset_contains(self->modules_version_seen, vkey);
        if (!already_seen) str_hset_insert(&self->modules_version_seen, vkey);
    } else {
        already_seen = str_hset_contains(self->modules_seen, module);
    }

    // save current module symbols, if any
    save_current_module_symbols(self);

    if (!already_seen) {
        str_hset_insert(&self->modules_seen, module);
    }

    if (is_main_function(module)) self->current_module = str_empty();
    else {
        // Note: do not use ast_arena, as it could be speculative and discarded
        self->current_module = str_copy(self->parent_alloc, module);
    }

    // Only reset symbols on first encounter during symbol collection pass.
    // During second pass, current_module_symbols may point to a hashmap in
    // module_symbols (set by load_module_symbols), and resetting it would
    // corrupt module_symbols.
    if (!already_seen && self->mode == mode_symbols) {
        hset_reset(self->current_module_symbols);
    }

    // load module symbols — for re-opened modules, this restores previously
    // collected symbols
    load_module_symbols(self);

    return 0;
}

int toplevel_hash_alias(parser *self, str_array words) {
    if (words.size != 3) {
        self->error.tag = tl_err_expected_hash_command;
        return ERROR_STOP;
    }
    str alias  = words.v[1];
    str source = words.v[2];

    // self-alias
    if (str_eq(source, alias)) {
        self->error.tag = tl_err_alias_self_alias;
        return ERROR_STOP;
    }
    // __ in alias name
    if (str_contains(alias, S("__"))) {
        self->error.tag = tl_err_double_underscore_in_identifier;
        return ERROR_STOP;
    }
    // reserved prefix on alias
    if (is_c_symbol(alias) || is_intrinsic(alias)) {
        self->error.tag = tl_err_alias_invalid_name;
        return ERROR_STOP;
    }
    // source is main
    if (is_main_function(source)) {
        self->error.tag = tl_err_alias_source_is_main;
        return ERROR_STOP;
    }
    // source is an existing alias name
    if (str_map_contains(self->module_aliases, source)) {
        self->error.tag = tl_err_alias_source_is_alias;
        return ERROR_STOP;
    }
    // source module must exist
    int source_known = str_hset_contains(self->modules_seen, source) ||
                       (self->opts.known_modules && str_map_contains(self->opts.known_modules, source));
    if (!source_known) {
        self->error.tag = tl_err_alias_source_not_found;
        return ERROR_STOP;
    }
    // alias name conflicts with real module
    int alias_is_module = str_hset_contains(self->modules_seen, alias) ||
                          (self->opts.known_modules && str_map_contains(self->opts.known_modules, alias));
    if (alias_is_module) {
        self->error.tag = tl_err_alias_conflicts_with_module;
        return ERROR_STOP;
    }
    // alias name already defined
    if (str_map_contains(self->module_aliases, alias)) {
        self->error.tag = tl_err_alias_already_defined;
        return ERROR_STOP;
    }

    str source_copy = str_copy(self->parent_alloc, source);
    str_map_set(&self->module_aliases, alias, &source_copy);
    return 0;
}

int toplevel_hash_unalias(parser *self, str alias) {
    if (!str_map_contains(self->module_aliases, alias)) {
        self->error.tag = tl_err_unalias_not_found;
        return ERROR_STOP;
    }
    str_map_erase(self->module_aliases, alias);
    return 0;
}

int toplevel_hash(parser *self) {
    if (a_try(self, a_hash_command)) return 1;
    ast_node *command = self->result;

    str_array words   = {.alloc = self->ast_arena};
    str_parse_words(command->hash_command.full, &words);

    if (words.size >= 2) {
        str cmd      = words.v[0];
        str argument = words.v[1];
        int res      = 0;
        parser_dbg(self, "hash: %s %s", str_cstr(&cmd), str_cstr(&argument));

        if (str_eq(cmd, S("unity_file"))) toplevel_hash_unity_file(self, argument);
        else if (str_eq(cmd, S("module"))) res = toplevel_hash_module(self, argument);
        else if (str_eq(cmd, S("alias"))) res = toplevel_hash_alias(self, words);
        else if (str_eq(cmd, S("unalias"))) res = toplevel_hash_unalias(self, argument);

        if (res) return res;
    }

    array_shrink(words);
    command->hash_command.words = (str_sized)sized_all(words);

    return 0;
}

int toplevel(parser *self) {

    self->error.tag = tl_err_ok;

    // Switch to speculative arena for all allocations during pattern matching
    allocator *permanent = self->ast_arena;
    self->ast_arena      = self->speculative;

    while (!is_eof(self)) {

        int res = 0;

        if (0 == a_try(self, toplevel_c_chunk)) goto success_hash;
        if (0 == (res = a_try(self, toplevel_hash))) goto success_hash;
        else if (ERROR_STOP == res) goto error;
        // Function alias: name = Module.func — must come before type alias
        // (a_type_identifier_base greedily parses dotted names like "Print.print").
        // In pass 2, skip the node — call sites are already rewritten.
        if (0 == a_try(self, toplevel_function_alias)) {
            if (self->mode == mode_source) {
                arena_reset(self->speculative);
                continue; // skip — no AST node needed
            }
            goto success;
        }

        if (0 == a_try(self, toplevel_type_alias)) goto success;

        // Tagged union must come before enum/struct since both start with identifier
        if (0 == (res = a_try(self, toplevel_tagged_union))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_enum))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_trait))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_struct))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_union))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_receiver_block))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_defun))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_assign))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_forward))) goto success;
        else if (ERROR_STOP == res) goto error;

        if (0 == (res = a_try(self, toplevel_symbol_annotation))) goto success;
        else if (ERROR_STOP == res) goto error;

        self->error.tag = tl_err_expected_toplevel;
        // Fall through to cleanup_fail

    cleanup_fail:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return 1;

    error:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return res;

    success:
        if (self->expect_module) {
            self->error.tag = tl_err_expected_module;
            goto cleanup_fail;
        }

    success_hash:
        if (!self->skip_module) {
            // Clone successful result to permanent arena
            self->result = ast_node_clone(permanent, self->result);
            arena_reset(self->speculative);
            self->ast_arena = permanent;
            return 0;
        }
        // skip_module: continue loop, reset speculative arena for next iteration
        arena_reset(self->speculative);
    }

    // EOF reached
    arena_reset(self->speculative);
    self->ast_arena = permanent;
    return 1;
}

int toplevel_funcall_only(parser *self) {

    self->error.tag = tl_err_ok;

    // Switch to speculative arena for all allocations during pattern matching
    allocator *permanent = self->ast_arena;
    self->ast_arena      = self->speculative;

    while (!is_eof(self)) {

        int res = 0;

        if (0 == (res = a_try(self, a_funcall))) goto success;
        else if (ERROR_STOP == res) goto error;

        self->error.tag = tl_err_expected_funcall;

    error:
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return res;

    success:
        // Clone successful result to permanent arena before resetting speculative
        self->result = ast_node_clone(permanent, self->result);
        arena_reset(self->speculative);
        self->ast_arena = permanent;
        return 0;
    }

    // EOF reached
    arena_reset(self->speculative);
    self->ast_arena = permanent;
    return 1;
}

int parser_next(parser *self) {
    while (1) {
        if (!self->tokenizer) {

            // A new tokenizer is created for each file being parsed.
            tokenizer_opts tok_opts   = {.defines = self->opts.defines};

            self->error.tag           = tl_err_ok;
            self->tokenizer_error.tag = tl_err_ok;

            // Parse the prelude string before any files.
            if (self->opts.prelude && !self->prelude_consumed) {
                self->prelude_consumed = 1;
                char_csized data       = {.v = self->opts.prelude, .size = strlen(self->opts.prelude)};

                tok_opts.input         = data;
                tok_opts.file          = "<prelude>";
                self->tokenizer        = tokenizer_create(self->parent_alloc, &tok_opts);
            } else {
                if ((i32)self->files_index >= (i32)self->files.size) {
                    self->error.tag = tl_err_eof;
                    return 1;
                }

                // free prior data
                if (self->current_file_data.v) {
                    alloc_free(self->file_arena, (void *)self->current_file_data.v);
                    self->current_file_data.v    = null;
                    self->current_file_data.size = 0;
                }

                // read file — normalize backslashes to forward slashes so that
                // #line directives don't contain sequences like \U that C
                // compilers interpret as UCN escapes.
                str file_str = self->files.v[self->files_index++];
#ifdef MOS_WINDOWS
                file_str = str_replace_char(self->parent_alloc, file_str, '\\', '/');
#endif
                char const *file = str_cstr(&file_str);

                if (self->opts.preloaded_path && 0 == strcmp(file, self->opts.preloaded_path)) {
                    // Use pre-loaded data (e.g. stdin); copy into file_arena so
                    // the normal free path works when moving to the next file.
                    if (self->opts.preloaded_size > 0) {
                        char *buf = alloc_malloc(self->file_arena, self->opts.preloaded_size);
                        memcpy(buf, self->opts.preloaded_data, self->opts.preloaded_size);
                        self->current_file_data =
                          (char_csized){.v = buf, .size = self->opts.preloaded_size};
                    } else {
                        self->current_file_data = (char_csized){.v = null, .size = 0};
                    }
                } else {
                    file_read(self->file_arena, file, (char **)&self->current_file_data.v,
                              &self->current_file_data.size);
                }

                // Swap per-file prefix map if available
                if (self->file_pkg_prefixes) {
                    hashmap *per_file         = str_map_get_ptr(self->file_pkg_prefixes, file_str);
                    self->module_pkg_prefixes = per_file ? per_file : self->opts.module_pkg_prefixes;
                }

                tok_opts.input      = self->current_file_data;
                tok_opts.file       = file;
                self->tokenizer     = tokenizer_create(self->parent_alloc, &tok_opts);
                self->expect_module = 1;
            }
        }

        int res = 0;
        if (mode_toplevel_funcall == self->mode) {
            res = toplevel_funcall_only(self);
        } else {
            res = toplevel(self);
        }

        if (0 == res) {
            self->result->file = self->error.file;
            self->result->line = self->error.line;
            self->result->col  = self->error.col;
            arena_reset(self->transient);
            return res;
        } else if (is_eof(self)) {
            if (self->tokenizer) tokenizer_destroy(&self->tokenizer);
            self->tokenizer   = null;
            self->tokens.size = 0;
            map_reset(self->module_aliases);

            // keep going with next file if possible
        } else {
            arena_reset(self->transient);
            return res;
        }
    }
}

int parser_parse_all(parser *self, ast_node_array *out) {

    self->mode = mode_source;

    int res    = 0;
    while (0 == (res = parser_next(self))) {
        ast_node *node;

        parser_result(self, &node);
        if (self->verbose) {
            str str = v2_ast_node_to_string(self->transient, node);
            parser_dbg(self, "parse_all: parsed node %s", str_cstr(&str));
        }
        array_push(*out, node);
    }

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_toplevel_funcalls(parser *self, ast_node_array *out) {
    self->mode = mode_toplevel_funcall;

    int res    = 0;
    while (0 == (res = parser_next(self))) {
        ast_node *node;

        parser_result(self, &node);
        array_push(*out, node);
    }

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_symbols(parser *self) {
    int res    = 0;
    self->mode = mode_symbols;

    while (0 == (res = parser_next(self))) {
        ast_node *node;
        parser_result(self, &node);
        if (self->verbose) {
            str str = v2_ast_node_to_string(self->transient, node);
            parser_dbg(self, "parse_all_symbols: parsed node %s", str_cstr(&str));
        }
    }

    save_current_module_symbols(self);

    if (is_eof(self)) return 0;

    return res;
}

int parser_parse_all_verbose(parser *p, ast_node_array *out) {
    p->verbose = 1;
    p->mode    = mode_source;

    parser_dbg(p, "begin parse");
    int res = parser_parse_all(p, out);
    parser_dbg(p, "end parse status %i", res);

    p->verbose = 0;
    return res;
}

void parser_result(parser *p, ast_node **handle) {
    if (handle) {
        *handle = p->result;
    }
}

void tokens_push_back(struct parser *p, struct token *tok) {
    array_push(p->tokens, *tok);
}

void tokens_shrink(struct parser *p, u32 n) {
    p->tokens.size = n;
}

//

str next_var_name(parser *self) {
    char buf[64];
    int  len = snprintf(buf, sizeof buf, "parser_var_%u", self->next_var_name++);
    return str_init_n(self->transient, buf, len);
}

//

void parser_report_errors(parser *self) {
    if (tl_err_ok == self->error.tag) return;

    fprintf(stderr, "%s:%u:%u: syntax error: %s\n", self->error.file, self->error.line, self->error.col,
            tl_error_tag_to_string(self->error.tag));
}
void parser_set_verbose(parser *self, int val) {
    self->verbose = val;
}

int too_many_arguments(parser *self) {
    self->error.tag = tl_err_too_many_arguments;
    return 1;
}

void parser_dbg(struct parser *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    int  spaces = self->indent_level * 2;

    char buf[256];
    int  offset = snprintf(buf, sizeof buf, "%*s", spaces, "");
    if (offset < 0) return;

    snprintf(buf + offset, sizeof buf - (u32)offset, "[parse] %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
