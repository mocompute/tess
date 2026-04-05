#include "transpile.h"

#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "ast_tags.h"
#include "hashmap.h"
#include "infer.h"
#include "parser.h"
#include "str.h"
#include "type.h"
#include "type_registry.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#define TRANSPILE_ARENA_SIZE     32 * 1024
#define TRANSPILE_TRANSIENT_SIZE 32 * 1024
#define TRANSPILE_BUILD_SIZE     32 * 1024

struct transpile {
    allocator        *parent;
    allocator        *arena;
    allocator        *transient;

    transpile_opts    opts;

    tl_type_registry *registry;
    ast_node_sized    nodes;
    ast_node_sized    synthesized_nodes;
    str_sized         hash_includes;
    tl_infer         *infer;
    tl_type_env      *env;
    tl_type_subs     *subs;
    hashmap          *toplevels;         // str => ast_node*
    hashmap          *specializations;   // str => str_array (generic name => specialization names)
    hashmap          *structs;           // u64 set
    hashmap          *context_generated; // str set
    hashmap          *thunks_generated;  // str set — C FFI thunks already emitted
    str_build         thunks_build;      // deferred buffer for C FFI thunk definitions

    hashmap          *interned_strings; // str → u64 (literal content → offset in interned block)
    u64               interned_offset;  // next write position in the interned block

    str_array         toplevels_sorted;

    str_build         build;

    u64               next_block;
    u64               next_res;

    int               no_line_directive;
    int               verbose;
};

typedef struct defer_scope {
    ast_node_sized      defers; // this body's ast_body.defers
    struct defer_scope *parent; // enclosing scope
} defer_scope;

typedef struct {
    str_sized free_variables;

    // inside a while statement body, update_label is used as the target of a continue statement.
    str update_label;

    // to minimize repetitive #line noise in the output file
    str last_line_directive;

    // instead of emitting output to evaluate an expression and push it on the result stack, return a str
    // which can be used as an lvalue for the expression.
    int want_lvalue;

    // emit arrow-typed symbols as raw C function pointers, not tl_closure (for C FFI args)
    int want_raw_fn_ptr;

    // the type of the expression just evaluated is effectively void, regardless of its declared type. For
    // example, _tl_fatal_ is -> any, but when it appears it should never be assigned to a result variable.
    int is_effective_void;

    // allocated closure: context fields are values (not pointers), access is direct (not dereference)
    int          is_allocated_closure;

    defer_scope *defers;              // innermost defer scope (linked list head)
    defer_scope *loop_defer_boundary; // snapshot at loop entry; break/continue stop here
    tl_monotype *func_return_type;    // return type of enclosing function (for cross-type try)

    int          is_struct_field; // suppress bare-function-name check for struct field access

} eval_ctx;

extern char const *embed_std_c;

static str         next_res(transpile *);
static str         next_label(transpile *);

static str         generate_body(transpile *, tl_monotype *, ast_node const *, eval_ctx *);
static str         generate_case(transpile *, tl_monotype *, ast_node const *, eval_ctx *);
static void        generate_decl(transpile *, str, tl_monotype *);
static void        generate_decl_pointer(transpile *, str, tl_monotype *);
static str         generate_expr(transpile *, tl_monotype *, ast_node const *, eval_ctx *);
static str         generate_inline_lambda(transpile *, tl_monotype *, ast_node const *, eval_ctx *);
static str         generate_let_in(transpile *, tl_monotype *, ast_node const *, eval_ctx *);
static str         generate_if_then_else(transpile *, ast_node const *, eval_ctx *);
static void        generate_main(transpile *);
static str         generate_funcall(transpile *, ast_node const *, eval_ctx *);
static str         generate_funcall_variadic(transpile *, ast_node const *, eval_ctx *);
static str         generate_funcall_intrinsic(transpile *, ast_node const *, eval_ctx *);
static void        generate_prototypes(transpile *, int);
static void        generate_structs(transpile *);
static void        generate_toplevel_values(transpile *);
static void        generate_toplevels(transpile *);
static void        generate_assign_lhs(transpile *, str);
static void        generate_assign(transpile *, str, str);
static void        generate_decl_init(transpile *, str, tl_monotype *, str);
static str         generate_context(transpile *, str_sized, eval_ctx *, int, ast_node *);
static lambda_closure_attrs toplevel_closure_attrs(transpile *, str);
static void                 generate_assign_field(transpile *, str, str, str);

static void                 cat(transpile *, str);
static void                 cat_nl(transpile *);
static void                 cat_sp(transpile *);
static void                 cat_op(transpile *, str);
static void                 cat_ampersand(transpile *);
static void                 cat_assign(transpile *);
static void                 cat_commasp(transpile *);
static void                 cat_dot(transpile *);
static void                 cat_open_round(transpile *);
static void                 cat_close_round(transpile *);
static void                 cat_open_curly(transpile *);
static void                 cat_open_curlyln(transpile *);
static void                 cat_close_curly(transpile *);
static void                 cat_close_square(transpile *);
static void                 cat_semicolon(transpile *);
static void                 cat_semicolonln(transpile *);
static void                 cat_star(transpile *);
static void                 cat_return(transpile *, str);
static void                 catln(transpile *, str);
static void                 cat_comment(transpile *, str);
static void                 cat_commentln(transpile *, str);
// static void        cat_double_slash(transpile *);
static void cat_close_curlyln(transpile *);
// static void        cat_i64(transpile *, i64);
// static void        cat_f64(transpile *, f64);

tl_monotype         *env_lookup(transpile *, str); // may be null
static str           mangle_fun(transpile *, str); // allocates transient
static int           should_generate(transpile *, str, tl_polytype *);
static str           type_to_c(transpile *, tl_polytype *);
static str           type_to_c_mono(transpile *, tl_monotype *);
static str           arrow_rhs_to_c(transpile *, tl_polytype *);
static str           arrow_to_c_params(transpile *, tl_polytype *, str_sized); // allocates transient
static void          build_arrow_to_c(transpile *, str_build *, tl_monotype *, str);
static str           ptr_to_arrow_to_c(transpile *, tl_monotype *);
static str           ptr_to_arrow_decl(transpile *, tl_monotype *, str);
static void          generate_function_signature(transpile *, str, tl_polytype *, str_sized);
noreturn static void exit_error(char const *file, u32 line, char const *restrict fmt, ...);
static void          update_type(transpile *, tl_monotype **);
static int           get_c_export_name(allocator *, ast_node *, str *out_export_name);
static int           is_c_exportable_type(tl_monotype *);
static void          validate_c_exports(transpile *);
static void          generate_c_exports(transpile *);
static int           has_c_exports(transpile *);

// Escape Tess identifiers that clash with C reserved keywords by prefixing them with tl_kw_.
// Returns the original name unchanged if it is not a C keyword.
static str escape_c_keyword(allocator *alloc, str name) {
    static const char *c_keywords[] = {"auto", "break", "case", "char", "const", "continue", "default",
                                       "do", "double", "else", "enum", "extern", "float", "for", "goto",
                                       "if", "inline", "int", "long", "register", "restrict", "return",
                                       "short", "signed", "sizeof", "static", "struct", "switch", "typedef",
                                       "union", "unsigned", "void", "volatile", "while",
                                       // C11
                                       "_Alignas", "_Alignof", "_Atomic", "_Bool", "_Complex", "_Generic",
                                       "_Imaginary", "_Noreturn", "_Static_assert", "_Thread_local", NULL};
    for (const char **kw = c_keywords; *kw; ++kw) {
        if (0 == str_cmp_c(name, *kw)) {
            return str_cat(alloc, S("tl_kw_"), name);
        }
    }
    return name;
}

//

// Generates function signature: "ret_type name(params)" or "ret_type (*name(params))(fp_params)"
// Handles both normal return types and function-pointer return types.
// Outputs directly to the transpile buffer.
static void generate_function_signature(transpile *self, str name, tl_polytype *type,
                                        str_sized param_names) {
    // Arrow return types are now tl_closure, so no special wrapping needed
    str ret = arrow_rhs_to_c(self, type);
    cat(self, ret);
    cat_sp(self);
    cat(self, mangle_fun(self, name));
    cat_open_round(self);
    cat(self, arrow_to_c_params(self, type, param_names));
    cat_close_round(self);
}

static void generate_prototypes(transpile *self, int decl_static) {
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;

        str          name = toplevel_name(node);
        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("missing type: '%s'", str_cstr(&name));

        // skip let nodes that are not specialized
        if (ast_node_is_let(node) && !ast_node_is_specialized(node)) continue;

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(self, name, type)) continue;

        if (decl_static) cat(self, S("static "));
        generate_function_signature(self, name, type, (str_sized){0});
        cat_semicolon(self);
        cat_nl(self);
    }
}

static str make_struct_name(allocator *alloc, tl_monotype *type, u64 *out_hash) {
    u64  hash = tl_monotype_hash64(type);

    char buf[128];
    snprintf(buf, sizeof buf, "tl_struct_%" PRIu64, hash);
    if (out_hash) *out_hash = hash;
    return str_init(alloc, buf);
}

static str make_tuple_field_name(u32 i) {
    char buf[64];
    snprintf(buf, sizeof buf, "x%u", i);
    return str_init_small(buf);
}

static str generate_tuple(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {

    str res = next_res(self);
    generate_decl(self, res, type);

    forall(i, type->list.xs) {
        str field = make_tuple_field_name(i);

        // evaluate tuple element
        str value = generate_expr(self, type->list.xs.v[i], node->tuple.elements[i], ctx);

        // assign to field
        generate_assign_field(self, res, field, value);
    }
    return res;
}

static void generate_struct(transpile *self, tl_monotype *type) {

    if (tl_monotype_is_tuple(type)) {
        u64 hash;
        str struct_name = make_struct_name(self->transient, type, &hash);
        if (hset_contains(self->structs, &hash, sizeof hash)) return;
        hset_insert(&self->structs, &hash, sizeof hash);

        cat(self, S("typedef struct "));
        cat(self, struct_name);
        catln(self, S(" {"));

        forall(i, type->list.xs) {
            str field = make_tuple_field_name(i);
            generate_decl(self, field, type->list.xs.v[i]);
        }

        cat(self, S("} "));
        cat(self, struct_name);
        cat_semicolonln(self);
    }
}

static void generate_structs(transpile *self) {
    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype *type = *(tl_polytype **)iter.data;

        if (type->type->tag == tl_tuple) {
            if (tl_polytype_is_scheme(type)) fatal("type is scheme");
            if (!tl_polytype_is_concrete(type)) fatal("struct type is not concrete");
            generate_struct(self, type->type);
        }
    }
    cat_nl(self);
}

static void generate_hash_includes(transpile *self) {
    forall(i, self->hash_includes) {
        cat(self, S("#include "));
        cat(self, self->hash_includes.v[i]);
        cat_nl(self);
    }
    cat_nl(self);
}

static void generate_ifc_blocks(transpile *self) {
    forall(i, self->nodes) {
        ast_node *node = self->nodes.v[i];
        if (!ast_node_is_ifc_block(node)) continue;

        cat(self, node->hash_command.full);
        cat_nl(self);
    }
    cat_nl(self);
}

// Resolve a monotype's canonical C name via the type environment (mirrors type_to_c user type logic).
static str resolve_canonical_name(transpile *self, tl_monotype *mono) {
    str name = mono->cons_inst->special_name;
    if (str_is_empty(name)) name = mono->cons_inst->def->name;
    tl_monotype *found = env_lookup(self, name);
    if (found && tl_monotype_is_inst(found)) {
        str sn = found->cons_inst->special_name;
        return str_is_empty(sn) ? found->cons_inst->def->name : sn;
    }
    return name;
}

static int should_skip_user_type(transpile *self, str name) {
    tl_monotype *env_type = env_lookup(self, name);
    if (!env_type) return 1;
    if (!tl_monotype_is_inst(env_type)) return 1;

    // Check canonicalised user type via the environment. If this node defines a type that is to be replaced
    // by a different name, don't emit it.
    str canonical = env_type->cons_inst->special_name;
    if (str_is_empty(canonical)) canonical = env_type->cons_inst->def->name;
    if (!str_eq(name, canonical)) return 1;

    return 0;
}

static void generate_one_user_type(transpile *self, ast_node *node) {
    if (!ast_node_is_utd(node)) return;
    str          name = toplevel_name(node);
    tl_polytype *poly = node->type;
    if (!tl_monotype_is_inst(poly->type)) fatal("not a type constructor instance");
    if (!tl_polytype_is_concrete(poly)) return;

    tl_type_constructor_def *def = poly->type->cons_inst->def;
    if (!def) fatal("missing type def");

    // enums have no instance arguments. They have only field names.
    if (!tl_monotype_is_enum(poly->type)) {

        if (should_skip_user_type(self, name)) return;

        if (node->user_type_def.is_union) cat(self, S("typedef union "));
        else cat(self, S("typedef struct "));
        cat(self, name);
        catln(self, S(" {"));

        assert(def->field_names.size == poly->type->cons_inst->args.size);
        if (def->field_names.size == 0) {
            // MSVC requires at least one member in a struct
            catln(self, S("char _empty;"));
        }
        forall(i, def->field_names) {
            generate_decl(self, def->field_names.v[i], poly->type->cons_inst->args.v[i]);
        }

        cat(self, S("} "));
        cat(self, name);
        cat_semicolonln(self);
        cat_nl(self);
    } else {
        // an enum
        cat(self, S("typedef enum {\n"));

        forall(i, def->field_names) {
            // mangle name: name_field
            cat(self, def->name);
            cat(self, S("__"));
            cat(self, def->field_names.v[i]);
            if (i + 1 < def->field_names.size) cat_commasp(self);
            cat_nl(self);
        }

        cat_close_curly(self);
        cat_sp(self);
        cat(self, def->name);
        cat_semicolonln(self);
        cat_nl(self);
    }
}

static void generate_one_user_type_forward(transpile *self, ast_node *node) {
    if (!ast_node_is_utd(node)) return;
    str          name = toplevel_name(node);
    tl_polytype *poly = node->type;
    if (!poly) fatal("missing type");
    if (!tl_monotype_is_inst(poly->type)) fatal("not a type constructor instance");
    if (!tl_polytype_is_concrete(poly)) return;

    tl_type_constructor_def *def = poly->type->cons_inst->def;
    if (!def) fatal("missing type def");

    // enums have no instance arguments. They have only field names.
    if (!tl_monotype_is_enum(poly->type)) {

        // type inference leaves unspecialized types in the environment. Don't emit them.
        if (should_skip_user_type(self, name)) return;

        if (node->user_type_def.is_union) cat(self, S("typedef union "));
        else cat(self, S("typedef struct "));
        cat(self, name);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    } else {
        // an enum
        cat(self, S("typedef enum"));
        cat_sp(self);
        cat(self, def->name);
        cat_semicolonln(self);
    }
}

static void generate_user_types(transpile *self) {

    // Emit forward declarations for concrete and specialized types.
    forall(i, self->nodes) {
        ast_node *node = self->nodes.v[i];
        if (ast_node_is_enum_def(node)) continue;
        generate_one_user_type_forward(self, node);
    }
    forall(i, self->synthesized_nodes) {
        ast_node *node = self->synthesized_nodes.v[i];
        generate_one_user_type_forward(self, node);
    }
    cat_nl(self);

    // Emit enums in program nodes.
    forall(i, self->nodes) {
        ast_node *node = self->nodes.v[i];
        if (!ast_node_is_enum_def(node)) continue;
        generate_one_user_type(self, node);
    }

    // Emit #ifc blocks before struct bodies — they may contain #define macros
    // used by CArray sizes (e.g. TL_ONCE_SIZE) or #include directives needed
    // by inline C helpers.
    generate_ifc_blocks(self);

    // Then emit concrete user types because they won't have been specialized.
    forall(i, self->nodes) {
        ast_node *node = self->nodes.v[i];
        if (ast_node_is_enum_def(node)) continue;
        generate_one_user_type(self, node);
    }

    // Emit specialized user types in dependency order: a type that contains another
    // synthesized type by value (not through Ptr) must be emitted after it.
    {
        u32      n           = self->synthesized_nodes.size;
        hashmap *synth_names = hset_create(self->transient, n * 2 + 1);
        hashmap *emitted     = hset_create(self->transient, n * 2 + 1);
        u8      *done        = alloc_calloc(self->transient, n, 1);

        for (u32 i = 0; i < n; i++) {
            ast_node *node = self->synthesized_nodes.v[i];
            if (ast_node_is_utd(node)) str_hset_insert(&synth_names, toplevel_name(node));
        }

        int progress = 1;
        while (progress) {
            progress = 0;
            for (u32 i = 0; i < n; i++) {
                if (done[i]) continue;
                ast_node *node = self->synthesized_nodes.v[i];

                if (!ast_node_is_utd(node) || !node->type || !tl_monotype_is_inst(node->type->type) ||
                    !tl_polytype_is_concrete(node->type)) {
                    done[i]  = 1;
                    progress = 1;
                    continue;
                }

                int               blocked = 0;
                tl_monotype_sized args    = node->type->type->cons_inst->args;
                forall(j, args) {
                    if (tl_monotype_is_inst(args.v[j])) {
                        str dep = resolve_canonical_name(self, args.v[j]);
                        if (str_hset_contains(synth_names, dep) && !str_hset_contains(emitted, dep)) {
                            blocked = 1;
                            break;
                        }
                    }
                }

                if (!blocked) {
                    generate_one_user_type(self, node);
                    str_hset_insert(&emitted, toplevel_name(node));
                    done[i]  = 1;
                    progress = 1;
                }
            }
        }

        for (u32 i = 0; i < n; i++) {
            if (!done[i]) generate_one_user_type(self, self->synthesized_nodes.v[i]);
        }
    }

    cat_nl(self);
}

static str context_name(transpile *self, str_sized fvs, int is_alloc) {
    // generate struct name using hash of fvs
    u64  hash = str_array_hash64(0, fvs);
    char buf[64];
    snprintf(buf, sizeof buf, is_alloc ? "tl_alloc_ctx_%" PRIu64 : "tl_ctx_%" PRIu64, hash);
    return str_init(self->transient, buf);
}

static void generate_context_struct(transpile *self, str_sized fvs, int is_alloc) {
    str name = context_name(self, fvs, is_alloc);
    if (str_hset_contains(self->context_generated, name)) return;
    str_hset_insert(&self->context_generated, name);

    // check types we don't want to emit because they are not concrete
    forall(i, fvs) {
        str          field      = fvs.v[i];
        tl_polytype *field_type = tl_type_env_lookup(self->env, field);
        if (!field_type) return;
        if (!tl_polytype_is_concrete(field_type)) return;
    }

    cat(self, S("typedef struct "));
    cat(self, name);
    cat_sp(self);
    cat_open_curlyln(self);

    forall(i, fvs) {
        str          field      = fvs.v[i];
        tl_polytype *field_type = tl_type_env_lookup(self->env, field);
        if (is_alloc) {
            // Allocated closure: value fields (copy of captured value)
            if (tl_arrow == field_type->type->tag) {
                // Arrow-typed capture → store as tl_closure (value, not pointer)
                cat(self, S("tl_closure "));
                cat(self, escape_c_keyword(self->transient, field));
                cat_semicolonln(self);
            } else {
                generate_decl(self, field, field_type->type);
            }
        } else {
            // Stack closure: pointer fields (reference to stack variable)
            generate_decl_pointer(self, field, field_type->type);
        }
        if (i + 1 < fvs.size) cat_nl(self);
    }

    cat_close_curly(self);
    cat_sp(self);
    cat(self, name);
    cat_semicolonln(self);
}

static str generate_ctx_var(transpile *self) {
    char buf[80];
    snprintf(buf, sizeof buf, "tl_ctx_var_%" PRIu64, self->next_res++);
    str out = str_init(self->transient, buf);
    cat(self, out);
    return out;
}

static int useful_name(str original, str name) {
    // heuristic to say if it's useful to output original name in a comment
    return !str_eq(original, name) && !str_is_empty(original) && str_cmp_nc(original, "tl_", 3);
}

static str remove_c_prefix(allocator *alloc, str name);

// Generate a C FFI thunk for a Tess function that needs to be passed as a raw C function pointer.
// The thunk strips the void* tl_ctx_raw parameter and calls the real function with NULL.
// Returns the thunk name.
static str generate_raw_fn_thunk(transpile *self, tl_monotype *type, str mangled_name) {
    // Build thunk name: prepend tl_cffi_ to the mangled name
    str thunk_name = str_cat(self->arena, S("tl_cffi_"), mangled_name);

    // Check if already generated
    if (str_hset_contains(self->thunks_generated, thunk_name)) return thunk_name;
    str_hset_insert(&self->thunks_generated, thunk_name);

    // Extract arrow components
    assert(tl_monotype_is_arrow(type));
    assert(type->list.xs.size == 2);
    tl_monotype      *ret_mono = tl_monotype_sized_last(type->list.xs);
    tl_monotype_sized params   = type->list.xs.v[0]->list.xs;

    str_build        *b        = &self->thunks_build;
    int               is_void  = tl_monotype_is_void(ret_mono) || tl_monotype_is_any(ret_mono);

    // static ret thunk_name(params...) { [return] real_name(NULL, params...); }
    str_build_cat(b, S("static "));
    str_build_cat(b, is_void ? S("void") : type_to_c_mono(self, ret_mono));
    str_build_cat(b, S(" "));
    str_build_cat(b, thunk_name);
    str_build_cat(b, S("("));

    if (!params.size) {
        str_build_cat(b, S("void"));
    }
    for (u32 i = 0; i < params.size; i++) {
        if (tl_monotype_is_arrow(params.v[i])) {
            str_build_cat(b, S("tl_closure"));
        } else {
            str_build_cat(b, type_to_c_mono(self, params.v[i]));
        }
        char pbuf[32];
        snprintf(pbuf, sizeof pbuf, " tl_p%u", i);
        str_build_cat(b, str_init_small(pbuf));
        if (i + 1 < params.size) str_build_cat(b, S(", "));
    }

    str_build_cat(b, S(") { "));
    if (!is_void) str_build_cat(b, S("return "));
    str_build_cat(b, mangled_name);
    str_build_cat(b, S("(NULL"));
    for (u32 i = 0; i < params.size; i++) {
        char pbuf[32];
        snprintf(pbuf, sizeof pbuf, ", tl_p%u", i);
        str_build_cat(b, str_init_small(pbuf));
    }
    str_build_cat(b, S("); }\n"));

    return thunk_name;
}

static str generate_expr_symbol(transpile *self, tl_monotype *type, str symbol_name, str original_name,
                                eval_ctx *ctx) {
    str name     = symbol_name;

    int is_arrow = tl_monotype_is_arrow(type);
    if (is_arrow) name = mangle_fun(self, name);

    // c_ prefixed symbols are always emitted literally
    if (is_c_symbol(name)) {
        return remove_c_prefix(self->transient, name);
    }

    // escape identifiers that clash with C reserved keywords
    name = escape_c_keyword(self->transient, name);

    if (ctx && str_array_contains_one(ctx->free_variables, symbol_name)) // unmangled name
    {
        if (ctx->is_allocated_closure) {
            // Allocated closure: direct access (value fields, not pointers)
            return str_cat(self->transient, S("tl_ctx->"), name);
        }
        // Stack closure: dereference pointer field
        return str_cat_4(self->transient, S("("), S("*tl_ctx->"), name, S(")"));
    }

    // Arrow-typed symbols that are toplevel functions: wrap in tl_closure (unless C FFI context)
    if (is_arrow && str_map_contains(self->toplevels, symbol_name)) {
        if (ctx && ctx->want_raw_fn_ptr) {
            // C FFI context: generate a thunk with C-compatible signature (no ctx parameter)
            return generate_raw_fn_thunk(self, type, name);
        }

        // Allocated closures: the tl_closure local variable was created at the let-in binding site.
        if (toplevel_closure_attrs(self, symbol_name).has_alloc) {
            return str_cat(self->transient, S("tl_cls_"), symbol_name);
        }

        if (ctx) {
            tl_polytype *poly = tl_type_env_lookup(self->env, symbol_name);
            if (poly && poly->type->list.fvs.size) {
                // Stack closure: construct context and embed in closure
                str ctx_var = generate_context(self, poly->type->list.fvs, ctx, 0, null);
                return str_cat_5(self->transient, S("(tl_closure){ .fn = (void*)"), name,
                                 S(", .ctx = (void*)&"), ctx_var, S(" }"));
            }
        }
        return str_cat_3(self->transient, S("(tl_closure){ .fn = (void*)"), name, S(", .ctx = NULL }"));
    }

    if (useful_name(original_name, name)) {
        // TODO: put this behind an option
        return str_cat_4(self->transient, S(" /*"), original_name, S("*/ "), name);
    } else {
        return name;
    }
}

static str generate_context(transpile *self, str_sized fvs, eval_ctx *ctx, int is_alloc,
                            ast_node *alloc_expr) {
    if (!fvs.size) return str_empty();
    str name = context_name(self, fvs, is_alloc);

    if (is_alloc) {
        // Allocated closure: heap-allocate context struct via allocator->malloc closure.
        // The alloc_expr has type Ptr[Allocator]. We call its malloc closure field:
        //   ((void*(*)(void*, AllocType*, size_t))alloc->malloc.fn)(alloc->malloc.ctx, alloc, sizeof(...))
        assert(alloc_expr);

        str allocator_c = generate_expr(self, null, alloc_expr, ctx);

        // Get the C type name for the Allocator struct from the alloc_expr's type
        tl_polytype *alloc_poly   = alloc_expr->type;
        str          alloc_type_c = S("void"); // fallback
        if (alloc_poly && tl_monotype_is_inst(alloc_poly->type) && alloc_poly->type->cons_inst->args.size) {
            alloc_type_c = type_to_c_mono(self, alloc_poly->type->cons_inst->args.v[0]);
        }

        cat(self, name);
        cat(self, S("* "));
        str ctx_var = generate_ctx_var(self);
        cat(self, S(" = ("));
        cat(self, name);
        // Indirect closure call: ((void*(*)(void*, AllocType*, unsigned long long))alloc->malloc.fn)
        //                        (alloc->malloc.ctx, alloc, sizeof(ctx_struct))
        cat(self, S("*)((/*any*/void*(*)(void*, "));
        cat(self, alloc_type_c);
        cat(self, S("*, size_t))"));
        cat(self, allocator_c);
        cat(self, S("->malloc.fn)("));
        cat(self, allocator_c);
        cat(self, S("->malloc.ctx, "));
        cat(self, allocator_c);
        cat(self, S(", sizeof("));
        cat(self, name);
        cat(self, S("));\n"));

        // Copy each captured value into the heap struct
        forall(i, fvs) {
            cat(self, ctx_var);
            cat(self, S("->"));
            cat(self, escape_c_keyword(self->transient, fvs.v[i]));
            cat_assign(self);

            str          field_name = fvs.v[i];
            tl_polytype *type       = tl_type_env_lookup(self->env, field_name);
            if (!type) exit_error(NULL, 0, "unknown free variable '%s' in closure", str_cstr(&field_name));
            field_name = generate_expr_symbol(self, type->type, field_name, str_empty(), ctx);
            cat(self, field_name);

            cat_semicolonln(self);
        }

        return ctx_var;
    }

    // Stack closure: declare context on stack with address-of for each field
    cat(self, name);
    cat_sp(self);
    str ctx_var = generate_ctx_var(self);
    cat_assign(self);
    cat_open_curly(self);

    forall(i, fvs) {
        cat_dot(self);
        cat(self, escape_c_keyword(self->transient, fvs.v[i]));
        cat_assign(self);
        cat_ampersand(self);
        cat_open_round(self);

        str          fname = fvs.v[i];
        tl_polytype *type  = tl_type_env_lookup(self->env, fname);
        if (!type) exit_error(NULL, 0, "unknown free variable '%s' in closure", str_cstr(&fname));
        fname = generate_expr_symbol(self, type->type, fname, str_empty(), ctx);
        cat(self, fname);

        cat_close_round(self);

        if (i + 1 < fvs.size) cat_commasp(self);
    }

    cat_close_curly(self);
    cat_semicolonln(self);

    return ctx_var;
}

// Look up closure attributes for a toplevel let-in lambda binding.
// Returns a zero-initialized struct if the name is not a let-in lambda.
static lambda_closure_attrs toplevel_closure_attrs(transpile *self, str name) {
    ast_node *node = ast_node_str_map_get(self->toplevels, name);
    if (!node || !ast_node_is_let_in_lambda(node)) return (lambda_closure_attrs){0};
    lambda_closure_attrs attrs =
      lambda_get_closure_attrs(self->transient, node->let_in.value->lambda_function.attributes);
    return attrs;
}

static void generate_toplevel_contexts(transpile *self) {

    hashmap_iterator iter = {0};
    str              name;
    void            *data;
    while (str_map_iter(self->env->map, &iter, &name, &data)) {
        tl_polytype *type = *(tl_polytype **)data;

        if (type->type->tag == tl_arrow && type->type->list.fvs.size) {
            int is_alloc = toplevel_closure_attrs(self, name).has_alloc;
            generate_context_struct(self, type->type->list.fvs, is_alloc);
        }
    }
    cat_nl(self);
}

static void generate_toplevel_values(transpile *self) {

    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);

        if (ast_node_is_let_in_lambda(node)) continue; // handled elsewhere
        if (!ast_node_is_let_in(node)) continue;
        str name = ast_node_str(node->let_in.name);
        if (is_c_symbol(name)) continue;

        tl_monotype *type = env_lookup(self, name);
        if (!type) type = node->let_in.value->type->type;
        if (!tl_monotype_is_concrete(type)) continue;

        cat(self, S("TL_THREAD_LOCAL "));
        generate_decl(self, name, type);
    }

    cat_nl(self);

    // tl_init function (namespaced in library mode to avoid collisions)
    if (self->opts.is_library && !str_is_empty(self->opts.lib_name)) {
        cat(self, S("TL_EXPORT void tl_init_"));
        cat(self, self->opts.lib_name);
        cat(self, S("(void) {\n"));
    } else if (self->opts.is_library) {
        cat(self, S("TL_EXPORT void tl_init(void) {\n"));
    } else {
        cat(self, S("static void tl_init(void) {\n"));
    }

    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);

        if (ast_node_is_let_in_lambda(node)) continue; // handled elsewhere
        if (!ast_node_is_let_in(node)) continue;
        str name = ast_node_str(node->let_in.name);
        if (is_c_symbol(name)) continue;
        tl_polytype *type = node->let_in.value->type;
        if (!tl_polytype_is_concrete(type)) continue;

        str value = generate_expr(self, type->type, node->let_in.value, null);
        generate_assign_lhs(self, escape_c_keyword(self->transient, name));
        cat(self, value);
        cat_semicolonln(self);
    }

    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);

        // call Module._init functions
        if (ast_node_is_let(node)) {
            str name = ast_node_str(node->let.name);
            if (is_module_init(name)) {
                cat(self, mangle_fun(self, name));
                cat(self, S("(NULL);\n"));
            }
        }
    }

    cat_close_curly(self);
    cat_nl(self);
}

static void generate_toplevels(transpile *self) {
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);

        if (ast_node_is_utd(node)) continue;
        str          name = toplevel_name(node);
        tl_polytype *poly = tl_type_env_lookup(self->env, name);
        if (!poly) fatal("missing type");

        // skip let nodes that are not specialized
        if (ast_node_is_let(node) && !ast_node_is_specialized(node)) {
            // fprintf(stderr, "DEBUG: skip not spec %s\n", str_cstr(&name));
            continue;
        }

        // skip non-arrow types, main, any generic types, intrinsics
        if (!should_generate(self, name, poly)) {
            // fprintf(stderr, "DEBUG: skip not generate %s\n", str_cstr(&name));
            continue;
        }

        assert(poly->type->list.xs.size == 2);
        tl_monotype *return_type = tl_monotype_sized_last(poly->type->list.xs);
        node                     = ast_node_str_map_get(self->toplevels, name);
        if (!node) {
            // fprintf(stderr, "DEBUG: skip not in toplevels %s\n", str_cstr(&name));
            continue; // e.g. std.tl funs that aren't used
        }
        ast_node *body = ast_node_body(node);
        if (!body) {
            // fprintf(stderr, "DEBUG: skip no body %s\n", str_cstr(&name));
            continue;
        }

        ast_arguments_iter iter       = ast_node_arguments_iter(node);
        str_array          params_str = {.alloc = self->transient};
        array_reserve(params_str, iter.nodes.size);

        ast_node *param;
        while ((param = ast_arguments_next(&iter))) {
            array_push(params_str, param->symbol.name);
        }

        str ret         = arrow_rhs_to_c(self, poly);
        int res_is_void = str_eq(ret, S("void"));
        str res         = str_empty();
        if (!res_is_void) {
            res = next_res(self);
        }

        generate_function_signature(self, name, poly, (str_sized)sized_all(params_str));
        cat_open_curlyln(self); // body

        assert(tl_monotype_is_list(poly->type));

        // Detect allocated closure via [[alloc]] attribute
        int is_alloc = toplevel_closure_attrs(self, name).has_alloc;

        // Unified closure: cast void* tl_ctx_raw to typed context pointer, or suppress unused warning
        if (poly->type->list.fvs.size) {
            str ctx_name = context_name(self, poly->type->list.fvs, is_alloc);
            cat(self, ctx_name);
            cat(self, S("* tl_ctx = ("));
            cat(self, ctx_name);
            cat(self, S("*)tl_ctx_raw;\n"));
        } else {
            cat(self, S("(void)tl_ctx_raw;\n"));
        }

        eval_ctx ctx      = {.free_variables       = poly->type->list.fvs,
                             .is_allocated_closure = is_alloc,
                             .func_return_type     = return_type};
        str      body_res = generate_expr(self, return_type, body, &ctx);
        if (!res_is_void && !str_is_empty(body_res) && !ctx.is_effective_void) {
            generate_decl(self, res, return_type);
            generate_assign(self, res, body_res);
            cat_return(self, res);
        } else if (ctx.is_effective_void && !str_is_empty(body_res)) {
            cat(self, body_res);
            cat_semicolonln(self);
        }
        cat_close_curly(self);
        cat_nl(self);
        cat_nl(self);
    }
}

static void generate_main(transpile *self) {
    ast_node const *main = ast_node_str_map_get(self->toplevels, S("main"));
    if (!main) exit_error(NULL, 0, "no 'main' function defined");
    if (ast_let != main->tag) fatal("logic error");

    tl_monotype      *type = env_lookup(self, S("main"));
    tl_monotype_sized args = {0};
    if (type && tl_monotype_is_arrow(type)) {
        args = tl_monotype_arrow_get_args(type);
    }

    if (!type || !args.size) {
        cat(self, S("int main(void) { tl_init(); return tl_fun_main(NULL); }\n"));
    } else if (1 == args.size) {
        cat(self, S("int main(int argc) { tl_init(); return tl_fun_main(NULL, argc); }\n"));
    } else if (2 == args.size) {
        cat(self,
            S("int main(int argc, char* argv[]) { tl_init(); return tl_fun_main(NULL, argc, argv); }\n"));
    }
}

static void generate_assign_lhs(transpile *self, str var) {
    cat(self, var);
    cat_assign(self);
}

static void generate_assign(transpile *self, str lhs, str rhs) {
    // Note: some callers do not check if the assignment is valid, so we do it here.
    if (str_is_empty(rhs) || str_is_empty(lhs)) return;
    cat(self, lhs);
    cat_assign(self);
    cat(self, rhs);
    cat_semicolonln(self);
}

// Emit combined declaration + initialization: TYPE name = value;
// Required for const types where C doesn't allow separate decl then assign.
static void generate_decl_init(transpile *self, str name, tl_monotype *type, str value) {
    if (str_is_empty(value) || str_is_empty(name)) return;
    name      = escape_c_keyword(self->transient, name);
    str typec = type_to_c_mono(self, type);
    cat(self, typec);
    cat_sp(self);
    cat(self, name);
    cat_assign(self);
    cat(self, value);
    cat_semicolonln(self);
}

static void generate_assign_op(transpile *self, str lhs, str rhs, str op) {
    cat(self, lhs);
    if (!str_is_empty(op)) cat_op(self, op);
    else cat_assign(self);
    cat(self, rhs);
    cat_semicolonln(self);
}

static void generate_assign_field(transpile *self, str lhs, str field, str rhs) {
    cat(self, lhs);
    cat_dot(self);
    cat(self, escape_c_keyword(self->transient, field));
    cat_assign(self);
    cat(self, rhs);
    cat_semicolonln(self);
}

static void generate_funcall_head(transpile *self, str name, str ctx_var, u32 n_args) {

    cat(self, mangle_fun(self, name));
    cat_open_round(self);

    // Unified closure convention: always pass context as first arg
    if (!str_is_empty(ctx_var)) {
        cat(self, S("(void*)&"));
        cat(self, ctx_var);
    } else {
        cat(self, S("NULL"));
    }
    if (n_args) cat_commasp(self);
}

static str_array generate_args(transpile *self, ast_node_sized args, tl_monotype *arrow, eval_ctx *ctx) {
    // generate args to match arrow type or type constructor
    str_array args_res = {.alloc = self->transient};
    if (!args.size) return args_res;
    array_reserve(args_res, args.size);

    tl_monotype_sized arr;
    if (tl_arrow == arrow->tag) {
        assert(2 == arrow->list.xs.size);
        assert(tl_tuple == arrow->list.xs.v[0]->tag);
        arr = arrow->list.xs.v[0]->list.xs;
    } else if (tl_cons_inst == arrow->tag) {
        arr = arrow->cons_inst->args;
    } else fatal("runtime error");

    assert(arr.size >= args.size);
    forall(i, args) {
        if (tl_monotype_is_void(arr.v[i])) {
            array_push(args_res, S("0"));
            continue;
        }
        str res = generate_expr(self, arr.v[i], args.v[i], ctx);
        array_push(args_res, res);
    }
    return args_res;
}

static int is_nil_result(tl_monotype *type) {
    return tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type) ||
           tl_monotype_is_weak(type) || tl_monotype_is_weak_int(type);
}

static int should_assign_result(eval_ctx *ctx, tl_monotype *type) {
    return !ctx->is_effective_void && !is_nil_result(type);
}

// Returns 1 if the node represents a value that should be assigned.
// ast_void means "declare without initialising", so it is the only
// node kind that suppresses the assignment.
static int should_assign_value(ast_node const *node) {
    return !ast_node_is_void(node);
}

static str generate_funcall_result(transpile *self, tl_monotype *type) {
    assert(tl_monotype_is_list(type));
    tl_monotype *funcall_result_type = tl_monotype_sized_last(type->list.xs);
    str          res                 = str_empty(); // empty signals void result

    if (!is_nil_result(funcall_result_type)) {
        res = next_res(self);
        generate_decl(self, res, funcall_result_type);
    }
    return res;
}

static str generate_type_constructor_named(transpile *self, ast_node const *node, eval_ctx *ctx) {
    // with named arguments (ast_assignment)

    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);
    assert(tl_monotype_is_inst(type));

    str                            res = next_res(self);
    tl_type_constructor_def const *def = type->cons_inst->def;

    generate_decl(self, res, type);

    forall(i, def->field_names) {
        // Allow partial construction, required for C unions
        if (i == node->named_application.n_arguments) break;
        ast_node const *arg = node->named_application.arguments[i];
        if (!ast_node_is_assignment(arg)) fatal("expected named assignment node");

        // Find field index matching name
        str field_name = ast_node_str(arg->assignment.name);
        i32 found      = tl_monotype_type_constructor_field_index(type, field_name);
        if (found == -1) {
            exit_error(node->file, node->line, "field '%s' not found in type '%s'", str_cstr(&field_name),
                       str_cstr(&name));
        }

        tl_monotype *field_type = type->cons_inst->args.v[found];
        if (tl_monotype_is_void(field_type)) {
            generate_assign_field(self, res, ast_node_str(arg->assignment.name), S("0"));
            continue;
        }

        str arg_value = generate_expr(self, field_type, arg->assignment.value, ctx);

        if (!str_is_empty(arg_value)) {
            cat(self, res);
            cat_dot(self);
            cat(self, escape_c_keyword(self->transient, ast_node_str(arg->assignment.name)));
            cat_assign(self);
            cat(self, arg_value);
            cat_semicolonln(self);
        }
    }

    return res;
}

static str generate_type_constructor(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_node_is_nfa(node));

    // divert if named arguments
    if (node->named_application.n_arguments && ast_node_is_assignment(node->named_application.arguments[0]))
        return generate_type_constructor_named(self, node, ctx);

    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);
    if (!tl_monotype_is_inst(type)) {
        exit_error(node->file, node->line,
                   "'%s' is not a known type constructor"
                   " (if this is not a function call, use a comma or semicolon to separate expressions)",
                   str_cstr(&name));
    }

    str                      res = next_res(self);
    tl_type_constructor_def *def = type->cons_inst->def;

    assert(def->field_names.size == node->named_application.n_arguments);
    assert(def->field_names.size == type->cons_inst->args.size);

    generate_decl(self, res, type);

    forall(i, def->field_names) {
        tl_monotype *field_type = type->cons_inst->args.v[i];
        if (tl_monotype_is_void(field_type)) {
            generate_assign_field(self, res, def->field_names.v[i], S("0"));
            continue;
        }
        str arg_value = generate_expr(self, field_type, node->named_application.arguments[i], ctx);

        if (!str_is_empty(arg_value)) {
            cat(self, res);
            cat_dot(self);
            cat(self, escape_c_keyword(self->transient, def->field_names.v[i]));
            cat_assign(self);
            cat(self, arg_value);
            cat_semicolonln(self);
        }
    }

    return res;
}

static str remove_c_prefix(allocator *alloc, str name) {
    span s = str_span(&name);
    s.buf += 2;
    s.len -= 2;
    return str_copy_span(alloc, s);
}

static str remove_c_struct_prefix(allocator *alloc, str name) {
    span s = str_span(&name);
    s.buf += 9;
    s.len -= 9;
    return str_copy_span(alloc, s);
}

static str generate_funcall_c(transpile *self, ast_node const *node, eval_ctx *ctx) {

    // a funcall to a c function is fundamentally different: we don't have type information on the
    // function or the arguments.

    // generate untyped arguments
    str name                = ast_node_name_original(node->named_application.name);
    name                    = remove_c_prefix(self->arena, name);

    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);

    // Look up the C function's type (used for both per-argument fn ptr dispatch and result type).
    eval_ctx          c_ctx       = ctx ? *ctx : (eval_ctx){0};
    tl_monotype      *type        = env_lookup(self, ast_node_str(node->named_application.name));
    tl_monotype_sized param_types = {0};
    if (type && tl_monotype_is_arrow(type)) param_types = tl_monotype_arrow_get_args(type);

    forall(i, args) {
        // Arrow-typed param → raw C fn ptr; non-arrow (e.g. Ptr[any]) → keep tl_closure.
        // No type info → assume raw fn ptr (backwards compat).
        c_ctx.want_raw_fn_ptr =
          !param_types.size || (i < param_types.size && tl_monotype_is_arrow(param_types.v[i]));
        str res = generate_expr(self, null, args.v[i], &c_ctx);
        array_push(args_res, res);
    }

    // declare variable to hold funcall result if it's not nil

    // Note: special case: if env lookup returns null, it could be a generic function that has not been
    // specialised. As a c function, it may be a valid part of the program regardless. If the funcall node
    // has a concrete type, use it to determine the result type.
    str res = str_empty();
    if (!type && node->type && tl_polytype_is_concrete(node->type)) {
        type = node->type->type;
        if (!is_nil_result(type)) {
            res = next_res(self);
            generate_decl(self, res, type);
        }
    } else if (!type && node->type && tl_monotype_is_ptr_to_tv(node->type->type)) {
        // treat Ptr(tv) as Ptr(any), which is rendered as void*
        type = tl_type_registry_ptr_any(self->registry);
        res  = next_res(self);
        generate_decl(self, res, type);
    }

    else if (type) {
        update_type(self, &type);
        res = generate_funcall_result(self, type);
    }

    // function call (C FFI — no tl_ctx parameter)
    if (!str_is_empty(res)) generate_assign_lhs(self, res);
    cat(self, name);
    cat_open_round(self);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static void generate_indirect_closure_call(transpile *self, str closure_name, tl_monotype *type,
                                           str_array args_res) {
    // Indirect call through tl_closure: ((ret(*)(void*, params...))name.fn)(name.ctx, args...)
    tl_monotype      *ret_mono = tl_monotype_sized_last(type->list.xs);
    str               ret_c    = type_to_c_mono(self, ret_mono);
    tl_monotype_sized params   = tl_monotype_arrow_get_args(type);

    cat(self, S("(("));
    cat(self, ret_c);
    cat(self, S("(*)(void*"));
    for (u32 pi = 0; pi < params.size; pi++) {
        cat_commasp(self);
        if (tl_monotype_is_arrow(params.v[pi])) {
            cat(self, S("tl_closure"));
        } else {
            cat(self, type_to_c_mono(self, params.v[pi]));
        }
    }
    cat(self, S("))"));
    cat(self, closure_name);
    cat(self, S(".fn)("));
    cat(self, closure_name);
    cat(self, S(".ctx"));

    for (u32 ai = 0; ai < args_res.size; ai++) {
        cat_commasp(self);
        cat(self, args_res.v[ai]);
    }
    cat_close_round(self);
}

static str generate_funcall_with_args(transpile *self, ast_node const *node, eval_ctx *ctx,
                                      str_array args_res) {

    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);

    // declare variable to hold funcall result if it's not nil
    str res = generate_funcall_result(self, type);

    assert(tl_monotype_is_list(type));

    // Check if this is a direct call to a known toplevel function or an indirect call through a closure.
    // Allocated closures use indirect calls — their context is created once at the binding site,
    // not fresh at each call site.
    int is_direct = str_map_contains(self->toplevels, name);
    int is_alloc  = is_direct && toplevel_closure_attrs(self, name).has_alloc;
    is_direct     = is_direct && !is_alloc;

    if (is_direct) {
        str ctx_var = str_empty();
        if (type->list.fvs.size) {
            ctx_var = generate_context(self, type->list.fvs, ctx, 0, null);
        }

        // Direct call: tl_fun_name(NULL or (void*)&ctx, args...)
        if (!str_is_empty(res)) generate_assign_lhs(self, res);
        generate_funcall_head(self, name, ctx_var, args_res.size);

        str_build b = str_build_init(self->transient, 128);
        str_build_join_array(&b, S(", "), args_res);
        cat(self, str_build_finish(&b));
        cat_close_round(self);
        cat_semicolonln(self);
    } else {
        // Indirect call through tl_closure variable
        // Resolve the variable name (may be through context, keyword-escaped, etc.)
        str closure_name =
          generate_expr_symbol(self, type, name, ast_node_name_original(node->named_application.name), ctx);

        if (!str_is_empty(res)) generate_assign_lhs(self, res);
        generate_indirect_closure_call(self, closure_name, type, args_res);
        cat_semicolonln(self);
    }

    return res;
}

// Emit a FormatSpec C struct literal into the transpile buffer.
// Returns the name of the temporary holding the spec.
static str emit_format_spec_literal(transpile *self, tl_format_spec const *spec, str fs_type_c) {
    str spec_tmp = next_res(self);
    cat(self, fs_type_c);
    cat(self, S(" "));
    cat(self, spec_tmp);
    cat(self, S(" = {"));

    char fill = spec->fill ? spec->fill : ' ';
    char buf[256];
    int  n = snprintf(buf, sizeof(buf),
                      ".fill = '%c', .align = %d, .sign = %d, .alt = %d, "
                       ".zero_pad = %d, .width = %d, .precision = %d, "
                       ".type_char = %d, .has_type_specific = %d",
                      fill, (int)spec->align, (int)spec->sign, spec->alt, spec->zero_pad, spec->width,
                      spec->precision, (int)spec->type_char, spec->has_type_specific);
    str_build_cat_n(&self->build, buf, (u32)n);

    cat(self, S("};\n"));
    return spec_tmp;
}

static str generate_funcall_variadic(transpile *self, ast_node const *node, eval_ctx *ctx) {
    // Variadic call: emit trait function calls for each variadic arg, pack into stack array,
    // construct Slice, and call the function with fixed args + slice.

    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);
    assert(type && tl_monotype_is_list(type));

    str res        = generate_funcall_result(self, type);

    u8  n_fixed    = node->named_application.n_fixed_args;
    u32 n_total    = node->named_application.n_arguments;
    u32 n_variadic = n_total - n_fixed;

    // Extract arrow params: (fixed_types..., Slice[ElemType]) -> ResultType
    tl_monotype      *params_tuple = type->list.xs.v[0];
    tl_monotype_sized params       = params_tuple->list.xs;
    assert(params.size == (u32)(n_fixed + 1));

    tl_monotype *slice_param = params.v[n_fixed]; // Slice[ElemType]
    // Slice[T] = { v: Ptr[T], size: CSize }, so args.v[0] = Ptr[T]; extract T.
    tl_monotype *elem_type = tl_monotype_ptr_target(slice_param->cons_inst->args.v[0]);

    // Generate fixed args
    str_array args_res = {.alloc = self->transient};
    array_reserve(args_res, (u32)(n_fixed + 1));
    for (u32 i = 0; i < n_fixed; i++) {
        str arg = generate_expr(self, params.v[i], node->named_application.arguments[i], ctx);
        array_push(args_res, arg);
    }

    // Generate variadic args: call trait function on each, collect temporaries
    str elem_type_c  = type_to_c_mono(self, elem_type);
    str slice_type_c = type_to_c_mono(self, slice_param);
    str slice_arg;

    if (n_variadic > 0) {
        str_array va_temps = {.alloc = self->transient};
        array_reserve(va_temps, n_variadic);

        tl_fstring_format const *ffmt        = node->named_application.fstring_fmt;
        tl_format_spec const    *fspecs      = ffmt ? ffmt->specs : null;
        u8 const                *uses_format = ffmt ? ffmt->uses_format : null;

        // Look up FormatSpec C type name (lazily, only if format specs are present).
        str fs_type_c = str_empty();
        str layout_fn = ffmt ? ffmt->layout_fn : str_empty();
        if (ffmt) {
            tl_polytype *fs_poly = tl_type_env_lookup(self->env, S("FormatSpec__FormatSpec"));
            if (fs_poly && fs_poly->type) fs_type_c = type_to_c_mono(self, fs_poly->type);
        }

        for (u32 i = 0; i < n_variadic; i++) {
            ast_node const *arg_node   = node->named_application.arguments[n_fixed + i];
            tl_monotype    *arg_type   = arg_node->type ? arg_node->type->type : null;
            str             arg_val    = generate_expr(self, arg_type, arg_node, ctx);

            str             impl_fn    = (node->named_application.variadic_impl_fns)
                                           ? node->named_application.variadic_impl_fns[i]
                                           : str_empty();

            int             use_fmt    = uses_format && uses_format[i];
            int             has_layout = fspecs && tl_format_spec_has_any(&fspecs[n_fixed + i]);

            // Emit FormatSpec struct literal if needed for this arg.
            str spec_tmp = str_empty();
            if ((use_fmt || has_layout) && !str_is_empty(fs_type_c)) {
                spec_tmp = emit_format_spec_literal(self, &fspecs[n_fixed + i], fs_type_c);
            }

            // Emit: ElemType tmpN = impl_fn(NULL, argN [, specN]);
            str tmp = next_res(self);
            generate_decl(self, tmp, elem_type);
            cat(self, tmp);
            cat_assign(self);
            cat(self, mangle_fun(self, impl_fn));
            if (use_fmt && !str_is_empty(spec_tmp)) {
                // Arity 2: to_string_format(NULL, arg, spec)
                cat(self, S("(NULL, "));
                cat(self, arg_val);
                cat(self, S(", "));
                cat(self, spec_tmp);
                cat(self, S(");\n"));
            } else {
                // Arity 1: to_string(NULL, arg)
                cat(self, S("(NULL, "));
                cat(self, arg_val);
                cat(self, S(");\n"));
            }

            // Apply layout (width/alignment/fill) if format spec is present.
            if (has_layout && !str_is_empty(layout_fn) && !str_is_empty(spec_tmp)) {
                str laid = next_res(self);
                generate_decl(self, laid, elem_type);
                cat(self, laid);
                cat_assign(self);
                cat(self, mangle_fun(self, layout_fn));
                cat(self, S("(NULL, "));
                cat(self, tmp);
                cat(self, S(", "));
                cat(self, spec_tmp);
                cat(self, S(");\n"));
                tmp = laid;
            }

            array_push(va_temps, tmp);
        }

        // Build stack array: ElemType tl_va_arr[] = {tmp0, tmp1, ...};
        str arr_name = next_res(self);
        cat(self, elem_type_c);
        cat(self, S(" "));
        cat(self, arr_name);
        cat(self, S("[] = {"));
        for (u32 i = 0; i < n_variadic; i++) {
            if (i) cat_commasp(self);
            cat(self, va_temps.v[i]);
        }
        cat(self, S("};\n"));

        // Construct Slice: (SliceType){arr_name, count}
        slice_arg = str_fmt(self->transient, "(%s){%s, %u}", str_cstr(&slice_type_c), str_cstr(&arr_name),
                            n_variadic);
    } else {
        // Zero variadic args: (SliceType){NULL, 0}
        slice_arg = str_fmt(self->transient, "(%s){NULL, 0}", str_cstr(&slice_type_c));
    }

    array_push(args_res, slice_arg);

    // Emit function call (same pattern as generate_funcall_with_args)
    int is_direct = str_map_contains(self->toplevels, name);
    int is_alloc  = is_direct && toplevel_closure_attrs(self, name).has_alloc;
    is_direct     = is_direct && !is_alloc;

    if (is_direct) {
        str ctx_var = str_empty();
        if (type->list.fvs.size) {
            ctx_var = generate_context(self, type->list.fvs, ctx, 0, null);
        }
        if (!str_is_empty(res)) generate_assign_lhs(self, res);
        generate_funcall_head(self, name, ctx_var, args_res.size);

        str_build b = str_build_init(self->transient, 128);
        str_build_join_array(&b, S(", "), args_res);
        cat(self, str_build_finish(&b));
        cat_close_round(self);
        cat_semicolonln(self);
    } else {
        str closure_name =
          generate_expr_symbol(self, type, name, ast_node_name_original(node->named_application.name), ctx);
        if (!str_is_empty(res)) generate_assign_lhs(self, res);
        generate_indirect_closure_call(self, closure_name, type, args_res);
        cat_semicolonln(self);
    }

    return res;
}

// Must match _STR_MAX_SMALL in String.tl and _STR_SMALL_TAG.
#define TL_SSO_MAX_SMALL 14
#define TL_SSO_SMALL_TAG 0x01

// Compute the byte length of a C string literal after escape processing.
// Input is the raw content between quotes (as stored in ast_string.symbol.name),
// where escape sequences are still literal (e.g., \n = two chars: '\' + 'n').
// Stops at first bare \0 escape to match existing strlen-based from_literal behavior.
static u64 cstr_decoded_length(str s) {
    u64         len = 0;
    char const *p   = str_buf(&s);
    char const *end = p + str_len(s);
    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            if (*p == 'x') {
                p += 2; // \xNN — skip two hex digits
            } else if (*p >= '0' && *p <= '7') {
                // Bare \0 (not followed by another octal digit) → stop, matches strlen
                if (*p == '0' && (p + 1 >= end || p[1] < '0' || p[1] > '7')) return len;
                p++;
                if (p < end && *p >= '0' && *p <= '7') p++;
                if (p < end && *p >= '0' && *p <= '7') p++;
            } else {
                p++; // \n, \t, \\, \", etc. — one decoded byte
            }
        } else {
            p++;
        }
        len++;
    }
    return len;
}

static unsigned char sso_tag_len(u64 n) {
    return (unsigned char)(((n & 0x0F) << 4) | TL_SSO_SMALL_TAG);
}

// Intern a big string literal. Returns the offset in the interned block.
// Deduplicates: identical literals share one entry.
static u64 intern_string(transpile *self, str content, u64 decoded_len) {
    void *existing = str_map_get(self->interned_strings, content);
    if (existing) return *(u64 *)existing;

    u64 offset = self->interned_offset;
    str_map_set(&self->interned_strings, content, &offset);
    self->interned_offset += decoded_len + 1; // +1 for null terminator
    return offset;
}

// Replace String.from_literal("...") with a compile-time String struct literal.
// Returns empty str to signal fallback to runtime from_literal (e.g., key too long for hashmap).
static str generate_interned_string(transpile *self, ast_node const *call_node, ast_node const *str_node) {
    str content     = str_node->symbol.name;
    u64 decoded_len = cstr_decoded_length(content);

    // str_map keys are limited to 254 bytes; fall back to runtime for very long literals
    if (str_len(content) >= UINT8_MAX) return str_empty();

    // Get the return type (String) from the function's arrow type
    tl_monotype *arrow = env_lookup(self, ast_node_str(call_node->named_application.name));
    // FIXME: this condition should be detected during type inference, not in the transpiler
    if (!arrow || !tl_monotype_is_list(arrow))
        fatal("String__from_literal is not defined — is '#import <String.tl>' missing?");
    tl_monotype *string_type = tl_monotype_sized_last(arrow->list.xs);
    str          type_name   = type_to_c_mono(self, string_type);

    str          res         = next_res(self);
    generate_decl(self, res, string_type);

    if (decoded_len <= TL_SSO_MAX_SMALL) {
        unsigned char tl = sso_tag_len(decoded_len);
        generate_assign_lhs(self, res);
        cat(self, str_fmt(self->transient, "(%.*s){ .ss = { .buf = \"%.*s\", .tag_len = 0x%02X } }",
                          str_ilen(type_name), str_buf(&type_name), str_ilen(content), str_buf(&content),
                          (unsigned)tl));
        cat_semicolonln(self);
    } else {
        u64 offset = intern_string(self, content, decoded_len);
        generate_assign_lhs(self, res);
        cat(self, str_fmt(self->transient,
                          "(%.*s){ .big = { .len = %llu, .buf = (char*)(tl_interned_strings_ + %llu) } }",
                          str_ilen(type_name), str_buf(&type_name), (unsigned long long)decoded_len,
                          (unsigned long long)offset));
        cat_semicolonln(self);
    }

    return res;
}

// Emit the static interned string block. Called after all toplevel code has been generated.
static void emit_interned_block(transpile *self) {
    if (self->interned_offset == 0) return;

    size_t n = map_size(self->interned_strings);
    typedef struct {
        u64 offset;
        str content;
    } intern_entry;
    intern_entry    *entries = alloc_malloc(self->transient, n * sizeof(intern_entry));

    hashmap_iterator iter    = {0};
    str              key;
    void            *data;
    size_t           idx = 0;
    while (str_map_iter(self->interned_strings, &iter, &key, &data)) {
        entries[idx++] = (intern_entry){.offset = *(u64 *)data, .content = key};
    }

    // Sort by offset (insertion sort — n is small)
    for (size_t i = 1; i < n; i++) {
        intern_entry tmp = entries[i];
        size_t       j   = i;
        while (j > 0 && entries[j - 1].offset > tmp.offset) {
            entries[j] = entries[j - 1];
            j--;
        }
        entries[j] = tmp;
    }

    cat(self, S("static const char tl_interned_strings_[] =\n"));
    for (size_t i = 0; i < n; i++) {
        cat(self, str_fmt(self->transient, "    \"%.*s\\0\"\n", str_ilen(entries[i].content),
                          str_buf(&entries[i].content)));
    }
    cat(self, S(";\n"));
}

static str generate_funcall(transpile *self, ast_node const *node, eval_ctx *ctx) {
    // Note: the main logic of this function is also duplicated in generate_binary_op.

    // A funcall can be a standard tl funcall, a c_ funcall, a type constructor, or a c_ type
    // constructor.

    assert(ast_node_is_nfa(node));
    str name = ast_node_str(node->named_application.name);

    // Compile-time string literal interning: replace String.from_literal("...") with struct literal
    if (str_starts_with(name, S("String__from_literal__1"))) {
        ast_node_sized args = ast_node_sized_from_ast_array((ast_node *)node);
        if (args.size == 1 && args.v[0]->tag == ast_string) {
            str r = generate_interned_string(self, node, args.v[0]);
            if (!str_is_empty(r)) return r; // empty = fallback to runtime
        }
    }

    if (is_intrinsic(name)) return generate_funcall_intrinsic(self, node, ctx);

    // c_ prefix: may be a c_ funcall or a c_ type constructor. If there is no type
    tl_monotype *type = env_lookup(self, name);

    // type constructor?
    if (type && tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

    // check c_ after type constructor
    if (is_c_symbol(name)) return generate_funcall_c(self, node, ctx);

    if (!type) {
        ast_node const *name_node = node->named_application.name;
        exit_error(name_node->file, name_node->line, "unknown function or function type: %s (%s:%i)",
                   str_cstr(&name), __FILE__, __LINE__);
    }

    // Function reference with explicit type args: emit as symbol (function pointer)
    if (node->named_application.is_function_reference) {
        return generate_expr_symbol(self, type, name, ast_node_name_original(node->named_application.name),
                                    ctx);
    }

    // type constructor?
    if (tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

    // Variadic call: divert to specialized codegen
    if (node->named_application.is_variadic_call) {
        return generate_funcall_variadic(self, node, ctx);
    }

    // generate arguments: an array of variables will hold their values
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = generate_args(self, args, type, ctx);

    return generate_funcall_with_args(self, node, ctx, args_res);
}

// Look up the type of a let-in value expression (returns NULL if not a symbol or not found).
static tl_monotype *let_in_val_type(transpile *self, ast_node const *node) {
    ast_node *val_node = node->let_in.value;
    if (ast_node_is_symbol(val_node)) return env_lookup(self, ast_node_str(val_node));
    return NULL;
}

// Emit the common trailing arguments for bounds-check macros: "source", "target", file, line
static void emit_bounds_check_tail(transpile *self, str source_c, str target_c, ast_node const *node) {
    char const *file = node->file ? node->file : "<unknown>";
    u32         line = node->line;
    cat(self, S(", \""));
    cat(self, source_c);
    cat(self, S("\", \""));
    cat(self, target_c);
    cat(self, S("\", \""));
    cat(self, str_init_static(file));
    cat(self, S("\", "));
    cat(self, str_init_u64(self->transient, line));
    cat(self, S(");\n"));
}

// Detect if a let-in binding is an explicit integer cast where the value type
// differs from the target (narrowing or cross-chain).
static int is_integer_narrowing_cast(tl_monotype *target, tl_monotype *val_type) {
    if (!val_type || !tl_monotype_is_integer_convertible(val_type)) return 0;
    if (str_eq(target->cons_inst->def->name, val_type->cons_inst->def->name)) return 0;
    return 1;
}

// Detect if a let-in binding is a float narrowing cast (wider float -> narrower float).
static int is_float_narrowing_cast(tl_monotype *target, tl_monotype *val_type) {
    if (!val_type || !tl_monotype_is_float_convertible(val_type)) return 0;
    if (target->cons_inst->def == val_type->cons_inst->def) return 0;
    int cmp = tl_monotype_compare_integer_width(target, val_type);
    return cmp < 0;
}

// Detect if a let-in value is a float (for float-to-integer cast bounds checking).
static int is_float_to_int_val(tl_monotype *val_type) {
    return val_type && tl_monotype_is_float_convertible(val_type);
}

// Emit a bounds-check macro call: one of four variants depending on source/target signedness.
static void emit_bounds_check(transpile *self, tl_monotype *target, tl_monotype *val_type, str value,
                              ast_node const *node) {
    char const *c_max = tl_monotype_integer_c_max(target);
    if (!c_max) return;

    str target_c           = type_to_c_mono(self, target);
    str source_c           = val_type ? type_to_c_mono(self, val_type) : S("(unknown)");
    int source_is_unsigned = val_type ? tl_monotype_is_unsigned_family(val_type) : 0;
    int target_is_unsigned = tl_monotype_is_unsigned_family(target);

    // Choose macro based on source/target signedness combination:
    // signed->signed:     tl_narrowing_assert(val, MIN, MAX, ...)
    // unsigned->unsigned:  tl_unsigned_narrowing_assert(val, MAX, ...)
    // unsigned->signed:    tl_unsigned_to_signed_assert(val, MAX, ...)
    // signed->unsigned:    tl_signed_to_unsigned_assert(val, MAX, ...)
    char const *macro_name;
    int         use_min = 0; // only signed->signed uses min
    if (!source_is_unsigned && !target_is_unsigned) {
        macro_name = "tl_narrowing_assert(";
        use_min    = 1;
    } else if (source_is_unsigned && target_is_unsigned) {
        macro_name = "tl_unsigned_narrowing_assert(";
    } else if (source_is_unsigned && !target_is_unsigned) {
        macro_name = "tl_unsigned_to_signed_assert(";
    } else {
        macro_name = "tl_signed_to_unsigned_assert(";
    }

    cat(self, str_init_static(macro_name));
    cat(self, value);
    if (use_min) {
        char const *c_min = tl_monotype_integer_c_min(target);
        cat(self, S(", "));
        cat(self, str_init_static(c_min));
    }
    cat(self, S(", "));
    cat(self, str_init_static(c_max));
    emit_bounds_check_tail(self, source_c, target_c, node);
}

// Emit a float narrowing bounds check: verifies the narrowed result is finite.
static void emit_float_narrowing_bounds_check(transpile *self, tl_monotype *target, tl_monotype *val_type,
                                              str value, ast_node const *node) {
    str source_c = val_type ? type_to_c_mono(self, val_type) : S("(unknown)");
    str target_c = type_to_c_mono(self, target);

    // tl_float_narrowing_assert(val, (target_c)val, "source", "target", file, line)
    cat(self, S("tl_float_narrowing_assert("));
    cat(self, value);
    cat(self, S(", ("));
    cat(self, target_c);
    cat(self, S(")"));
    cat(self, value);
    emit_bounds_check_tail(self, source_c, target_c, node);
}

// Emit a float-to-integer bounds check: verifies the float is within the integer range and not NaN.
static void emit_float_to_int_bounds_check(transpile *self, tl_monotype *target, tl_monotype *val_type,
                                           str value, ast_node const *node) {
    char const *c_max = tl_monotype_integer_c_max(target);
    if (!c_max) return;

    str         source_c           = val_type ? type_to_c_mono(self, val_type) : S("(unknown)");
    str         target_c           = type_to_c_mono(self, target);

    int         target_is_unsigned = tl_monotype_is_unsigned_family(target);
    char const *c_min              = target_is_unsigned ? "0" : tl_monotype_integer_c_min(target);
    if (!c_min) c_min = "0";

    // tl_float_to_int_assert(val, MIN, MAX, "source", "target", file, line)
    cat(self, S("tl_float_to_int_assert("));
    cat(self, value);
    cat(self, S(", "));
    cat(self, str_init_static(c_min));
    cat(self, S(", "));
    cat(self, str_init_static(c_max));
    emit_bounds_check_tail(self, source_c, target_c, node);
}

static void emit_closure_binding(transpile *self, str spec_name, str ctx_var) {
    str fn_name  = mangle_fun(self, spec_name);
    str cls_name = str_cat(self->transient, S("tl_cls_"), spec_name);

    cat(self, S("tl_closure "));
    cat(self, cls_name);
    cat(self, S(" = (tl_closure){ .fn = (void*)"));
    cat(self, fn_name);
    if (!str_is_empty(ctx_var)) {
        cat(self, S(", .ctx = (void*)"));
        cat(self, ctx_var);
    } else {
        cat(self, S(", .ctx = NULL"));
    }
    cat(self, S(" };\n"));
}

static void emit_line_directive(transpile *self, ast_node const *node, eval_ctx *ctx) {
    if (!self->no_line_directive && ctx && node->file && node->file[0]) {
        str line = str_fmt(self->transient, "#line %u \"%s\"\n", node->line, node->file);
        if (!str_eq(line, ctx->last_line_directive)) {
            ctx->last_line_directive = line;
            cat(self, line);
        }
    }
}

static str generate_let_in_lambda(transpile *self, tl_monotype *result_type, ast_node const *node,
                                  eval_ctx *ctx) {

    // For allocated closures: create local tl_closure variable(s) with heap-allocated context.
    // A polymorphic closure may have multiple specializations, each needing its own binding
    // (same context, different function pointer).
    str                  name = ast_node_str(node->let_in.name);
    lambda_closure_attrs alloc_attrs =
      lambda_get_closure_attrs(self->transient, node->let_in.value->lambda_function.attributes);
    if (alloc_attrs.has_alloc) {
        tl_polytype *poly    = tl_type_env_lookup(self->env, name);

        str          ctx_var = str_empty();
        if (poly && poly->type->list.fvs.size)
            ctx_var = generate_context(self, poly->type->list.fvs, ctx, 1, alloc_attrs.alloc_expr);

        if (str_map_contains(self->toplevels, name)) {
            // Monomorphic case: binding was renamed to the single specialization.
            emit_closure_binding(self, name, ctx_var);
        } else {
            // Polymorphic case: binding keeps the generic name (removed from toplevels).
            // Look up all specializations from the inference-side map.
            str_array *specs = str_map_get(self->specializations, name);
            if (specs) {
                forall(i, *specs) {
                    str spec = specs->v[i];
                    if (!toplevel_closure_attrs(self, spec).has_alloc) continue;
                    emit_closure_binding(self, spec, ctx_var);
                }
            }
        }
    }

    // don't declare or assign to name, because it is hoisted to a toplevel.

    str body = generate_expr(self, null, node->let_in.body, ctx);
    if (!str_is_empty(body) && should_assign_result(ctx, result_type)) {
        str res = next_res(self);
        generate_decl(self, res, result_type);
        generate_assign(self, res, body);
        return res;
    } else if (!str_is_empty(body)) {
        cat(self, body);
        cat_semicolonln(self);
    }
    return str_empty();
}

// If the value is void, just declare the binding without initialising it and return 1.
static int emit_void_decl(transpile *self, str name, tl_monotype *type, ast_node const *node) {
    if (should_assign_value(node->let_in.value)) return 0;
    generate_decl(self, name, type);
    return 1;
}

// If the binding's type is indeterminate (type variable or empty value), report an error and return 1.
static int check_indeterminate_type(tl_monotype *type, str value, ast_node const *node) {
    if (!tl_monotype_is_tv(type) && !str_is_empty(value)) return 0;
    if (ast_node_is_symbol(node->let_in.value)) {
        str value_str = ast_node_str(node->let_in.value);
        exit_error(node->let_in.value->file, node->let_in.value->line, "unknown symbol: %s",
                   str_cstr(&value_str));
    } else {
        // TODO: improve error
        str original = ast_node_name_original(node->let_in.name);
        exit_error(node->let_in.value->file, node->let_in.value->line,
                   "value has incomplete type information: %s", str_cstr(&original));
    }
    return 1;
}

// If the type is non-concrete, emit what we can and return 1. Non-concrete types arise when the
// variable is never referenced; we still emit c_ symbols and non-arrow values because return type
// information is not always available for c_ functions.
static int emit_non_concrete(transpile *self, str name, tl_monotype *type, str value,
                             ast_node const *node) {
    if (tl_monotype_is_concrete(type)) return 0;
    if (is_c_symbol(value) || !tl_monotype_is_arrow(type)) {
        generate_decl(self, name, type);
        if (should_assign_value(node->let_in.value)) generate_assign(self, name, value);
    }
    return 1;
}

// Emit numeric bounds checks for narrowing conversions (integer or float).
static void emit_numeric_bounds_checks(transpile *self, tl_monotype *inner_type, str value,
                                       ast_node const *node) {
    tl_monotype *val_type = let_in_val_type(self, node);
    if (tl_monotype_is_integer_convertible(inner_type)) {
        if (is_integer_narrowing_cast(inner_type, val_type))
            emit_bounds_check(self, inner_type, val_type, value, node);
        else if (is_float_to_int_val(val_type))
            emit_float_to_int_bounds_check(self, inner_type, val_type, value, node);
    } else {
        if (is_float_narrowing_cast(inner_type, val_type))
            emit_float_narrowing_bounds_check(self, inner_type, val_type, value, node);
    }
}

// Wrap the value in an explicit C cast for pointers and numeric types.
static str cast_value(transpile *self, tl_monotype *inner_type, str value, ast_node const *node) {
    if (!tl_monotype_is_ptr(inner_type) && !tl_monotype_is_integer_convertible(inner_type) &&
        !tl_monotype_is_float_convertible(inner_type))
        return value;
    if (!tl_monotype_is_ptr(inner_type)) emit_numeric_bounds_checks(self, inner_type, value, node);
    return str_cat_4(self->transient, S("("), type_to_c_mono(self, inner_type), S(")"), value);
}

// Return 1 if the node is a compile-time constant literal.
static int is_const_literal(ast_node const *node) {
    ast_tag t = node->tag;
    if (t == ast_i64 || t == ast_u64 || t == ast_i64_z || t == ast_u64_zu || t == ast_f64 ||
        t == ast_char || t == ast_bool)
        return 1;
    if (t == ast_body) {
        for (u32 i = 0; i < node->body.expressions.size; i++)
            if (!is_const_literal(node->body.expressions.v[i])) return 0;
        return node->body.expressions.size > 0;
    }
    return 0;
}

// Return the raw C literal string for a constant AST node (no temporaries emitted).
static str literal_to_c(transpile *self, ast_node const *node) {
    ast_tag t = node->tag;
    if (t == ast_i64) return str_init_i64(self->transient, node->i64.val);
    if (t == ast_i64_z) return str_init_i64(self->transient, node->i64_z.val);
    if (t == ast_u64)
        return str_cat(self->transient, str_init_u64(self->transient, node->u64.val), S("ULL"));
    if (t == ast_u64_zu)
        return str_cat(self->transient, str_init_u64(self->transient, node->u64_zu.val), S("ULL"));
    if (t == ast_f64) return str_init_f64(self->transient, node->f64.val);
    if (t == ast_char) return str_cat_3(self->transient, S("'"), node->symbol.name, S("'"));
    if (t == ast_bool) return node->bool_.val ? S("1 /*true*/") : S("0 /*false*/");
    return str_empty();
}

// Emit a C brace-enclosed initializer list for a CArray literal body, recursing for nested CArrays.
static void emit_brace_list(transpile *self, tl_monotype *elem_type, ast_node const *body) {
    cat_open_curly(self);
    for (u32 i = 0; i < body->body.expressions.size; i++) {
        if (i) cat_commasp(self);
        ast_node const *expr = body->body.expressions.v[i];
        if (ast_node_is_body(expr) && tl_monotype_is_carray(elem_type)) {
            emit_brace_list(self, tl_monotype_carray_element(elem_type), expr);
        } else {
            cat(self, literal_to_c(self, expr));
        }
    }
    cat_close_curly(self);
}

// Emit a CArray literal binding.  Returns 1 if handled, 0 if not a CArray literal.
static int emit_carray_literal(transpile *self, str name, tl_monotype *type, ast_node const *node,
                               eval_ctx *ctx) {
    int          is_const = tl_monotype_is_const(type);
    tl_monotype *inner    = tl_monotype_strip_const(type);
    if (!tl_monotype_is_carray(inner)) return 0;

    ast_node const *value = node->let_in.value;
    if (!ast_node_is_body(value) || value->body.defers.size != 0) return 0;

    tl_monotype *elem_type = tl_monotype_carray_element(inner);
    i32          count     = tl_monotype_carray_count(inner);

    // Check if all elements are compile-time constants
    int all_const = 1;
    for (u32 i = 0; i < value->body.expressions.size; i++) {
        if (!is_const_literal(value->body.expressions.v[i])) {
            all_const = 0;
            break;
        }
    }

    if (all_const) {
        // Static path: T name[N1][N2]... = {{e0, e1}, ...};
        // Build dimension suffixes and find innermost element type for multi-dimensional arrays.
        str          dims = str_fmt(self->transient, "[%d]", count);
        tl_monotype *base = elem_type;
        while (tl_monotype_is_carray(base)) {
            dims = str_cat(self->transient, dims,
                           str_fmt(self->transient, "[%d]", tl_monotype_carray_count(base)));
            base = tl_monotype_carray_element(base);
        }
        str base_c = type_to_c_mono(self, base);
        if (is_const) cat(self, S("const "));
        cat(self,
            str_fmt(self->transient, "%s %s%s = ", str_cstr(&base_c), str_cstr(&name), str_cstr(&dims)));
        emit_brace_list(self, elem_type, value);
        cat_semicolonln(self);
    } else {
        // Runtime path: T name[N]; name[0] = e0; ...
        generate_decl(self, name, inner);
        for (i32 i = 0; i < count; i++) {
            str val = generate_expr(self, elem_type, value->body.expressions.v[i], ctx);
            cat(self, str_fmt(self->transient, "%s[%d]", str_cstr(&name), i));
            cat_assign(self);
            cat(self, val);
            cat_semicolonln(self);
        }
    }
    return 1;
}

// Emit a single let-in binding: declaration, optional value assignment, casts, and bounds checks.
static void emit_let_binding(transpile *self, ast_node const *node, eval_ctx *ctx) {
    str          name = ast_node_str(node->let_in.name);
    tl_monotype *type = env_lookup(self, name); // may be null
    if (!type) return;

    name = escape_c_keyword(self->transient, name);

    if (emit_void_decl(self, name, type, node)) return;

    if (emit_carray_literal(self, name, type, node, ctx)) return;

    // For pointer cast bindings (e.g. data: Ptr[CChar] := arr->data where arr->data is Ptr[T]),
    // generate the value expression using the actual RHS type rather than the annotation type.
    // This prevents C implicit-pointer-conversion warnings: the intermediate temp gets the
    // correct source pointer type, and cast_value() below emits the explicit (T*) cast.
    tl_monotype *gen_type  = type;
    if (tl_monotype_is_ptr(tl_monotype_strip_const(type)) && node->let_in.value->type) {
        tl_monotype *val_inner = tl_monotype_strip_const(node->let_in.value->type->type);
        if (tl_monotype_is_ptr(val_inner)) gen_type = val_inner;
    }
    str value = generate_expr(self, gen_type, node->let_in.value, ctx);

    if (check_indeterminate_type(type, value, node)) return;

    if (emit_non_concrete(self, name, type, value, node)) return;

    if (!should_assign_result(ctx, type)) return;

    int          is_const_bind = tl_monotype_is_const(type);
    tl_monotype *inner_type    = tl_monotype_strip_const(type);
    str          emit_value    = cast_value(self, inner_type, value, node);

    if (is_const_bind) {
        // Const types require combined declaration+initialization in C.
        generate_decl_init(self, name, type, emit_value);
    } else {
        generate_decl(self, name, type);
        if (!str_eq(emit_value, value)) {
            cat(self, name);
            cat_assign(self);
            cat(self, emit_value);
            cat_semicolonln(self);
        } else {
            generate_assign(self, name, value);
        }
    }
}

static str generate_let_in(transpile *self, tl_monotype *result_type, ast_node const *node, eval_ctx *ctx) {
    if (ast_node_is_let_in_lambda(node)) return generate_let_in_lambda(self, result_type, node, ctx);
    assert(ast_node_is_let_in(node));

    // Iteratively process chains of regular let-in bindings to avoid
    // O(N) recursion depth (stack exhaustion on large functions).
    for (;;) {
        emit_let_binding(self, node, ctx);

        // If the body is another regular let-in, continue iterating instead of recursing.
        // Also unwrap ast_body wrappers (no defers) that the parser inserts between
        // let-in nodes — process non-final expressions (reassignments, calls, etc.)
        // inline and continue the loop with the final expression.
        ast_node const *next = node->let_in.body;
        if (ast_node_is_body(next) && next->body.defers.size == 0 && next->body.expressions.size > 0) {
            u32             n    = next->body.expressions.size;
            ast_node const *last = next->body.expressions.v[n - 1];
            if (ast_node_is_let_in(last) && !ast_node_is_let_in_lambda(last)) {
                for (u32 i = 0; i + 1 < n; i++) generate_expr(self, null, next->body.expressions.v[i], ctx);
                node = last;
                emit_line_directive(self, node, ctx);
                if (ctx) ctx->is_effective_void = 0;
                continue;
            }
        } else if (ast_node_is_let_in(next) && !ast_node_is_let_in_lambda(next)) {
            node = next;
            emit_line_directive(self, node, ctx);
            if (ctx) ctx->is_effective_void = 0;
            continue;
        }
        break;
    }

    str body = generate_expr(self, null, node->let_in.body, ctx);
    if (!str_is_empty(body) && should_assign_result(ctx, result_type)) {
        str res = next_res(self);
        generate_decl(self, res, result_type);
        if (should_assign_value(node->let_in.body)) generate_assign(self, res, body);
        return res;
    } else {
        return body;
    }
}

static str generate_if_then_else(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_if_then_else == node->tag);
    ast_node const *cond        = node->if_then_else.condition;
    ast_node const *yes         = node->if_then_else.yes;
    ast_node const *no          = node->if_then_else.no;
    tl_monotype    *result_type = yes->type->type;

    // Compile-time type predicates: skip dead branches entirely to avoid generating
    // invalid C code (e.g., accessing Result fields on an Option type).
    if (ast_type_predicate == cond->tag) {
        if (cond->type_predicate.is_valid) {
            // Predicate is true: emit only the yes branch
            str res = str_empty();
            if (should_assign_result(ctx, result_type)) {
                res = next_res(self);
                generate_decl(self, res, result_type);
            }
            str yes_str = generate_expr(self, null, yes, ctx);
            if (should_assign_result(ctx, result_type)) {
                generate_assign(self, res, yes_str);
            } else {
                cat(self, yes_str);
                cat_semicolonln(self);
            }
            return res;
        } else {
            // Predicate is false: emit only the else branch
            if (no && !ast_node_is_nil_or_void(no)) {
                return generate_expr(self, null, no, ctx);
            }
            return str_empty();
        }
    }

    str cond_str = generate_expr(self, null, cond, ctx);

    str res      = str_empty();
    if (should_assign_result(ctx, result_type)) {
        res = next_res(self);
        generate_decl(self, res, result_type);
    }

    cat(self, S("if ("));
    cat(self, cond_str);
    cat(self, S(") {\n"));

    str yes_str = generate_expr(self, null, yes, ctx);
    if (should_assign_result(ctx, result_type)) {
        // if there is no 'no' case, this is an if statement and has no result
        generate_assign(self, res, yes_str);
    } else {
        cat(self, yes_str);
        cat_semicolonln(self);
    }
    cat(self, S("}\n"));

    if (no && !ast_node_is_nil_or_void(no)) {
        cat(self, S("else {\n"));
        str no_str = generate_expr(self, null, no, ctx);
        if (should_assign_result(ctx, no->type->type)) {
            generate_assign(self, res, no_str);
        } else {
            cat(self, no_str);
            cat_semicolonln(self);
        }
        cat(self, S("}\n"));
    }

    if (ctx && !str_is_empty(res)) ctx->is_effective_void = 0;
    return res;
}

static str generate_inline_lambda_with_args(transpile *self, tl_monotype *result_type, ast_node const *node,
                                            eval_ctx *ctx, str_array args_res) {
    assert(ast_node_is_lambda_application(node));

    ast_node_sized params = ast_node_sized_from_ast_array(node->lambda_application.lambda);

    if (node->lambda_application.lambda->type->quantifiers.size) fatal("type scheme");
    tl_monotype *arrow = node->lambda_application.lambda->type->type;

    // declare variable to hold funcall result if it's not nil
    str res = generate_funcall_result(self, arrow);

    // establish lexical scope for parameters in case of repeated applications
    cat_open_curlyln(self);

    // initialise parameters
    forall(i, params) {
        ast_node const *param = params.v[i];
        // if (ast_node_is_nil_or_void(param)) break;
        assert(ast_node_is_symbol(param));
        assert(!param->type->quantifiers.size);

        str pname = escape_c_keyword(self->transient, param->symbol.name);
        generate_decl(self, pname, param->type->type);
        generate_assign_lhs(self, pname);
        cat(self, args_res.v[i]);
        cat_semicolonln(self);
    }

    // generate lambda body
    str lambda_res =
      generate_expr(self, result_type, node->lambda_application.lambda->lambda_function.body, ctx);
    if (!str_is_empty(res) && !ctx->is_effective_void) {
        generate_assign_lhs(self, res);
        cat(self, lambda_res);
        cat_semicolonln(self);
    } else if (!str_is_empty(lambda_res)) {
        cat(self, lambda_res);
        cat_semicolonln(self);
    }

    // close lexical scope
    cat_close_curlyln(self);
    return res;
}

static str generate_inline_lambda(transpile *self, tl_monotype *result_type, ast_node const *node,
                                  eval_ctx *ctx) {
    assert(ast_node_is_lambda_application(node));

    ast_node_sized params = ast_node_sized_from_ast_array(node->lambda_application.lambda);
    ast_node_sized args   = ast_node_sized_from_ast_array((ast_node *)node);
    if (params.size != args.size) exit_error(node->file, node->line, "lambda parameter count mismatch");

    if (node->lambda_application.lambda->type->quantifiers.size) fatal("type scheme");
    tl_monotype *arrow    = node->lambda_application.lambda->type->type;

    str_array    args_res = generate_args(self, args, arrow, ctx);
    assert(args_res.size == params.size);

    return generate_inline_lambda_with_args(self, result_type, node, ctx, args_res);
}

static str generate_str(transpile *self, str expr, tl_monotype *type) {
    if (str_is_empty(expr)) return expr;
    str res = next_res(self);
    generate_decl(self, res, type);
    generate_assign_lhs(self, res);
    cat(self, expr);
    cat_semicolonln(self);
    return res;
}

static void emit_defers_until(transpile *self, eval_ctx *ctx, defer_scope *boundary) {
    int save_is_effective_void = ctx->is_effective_void;
    for (defer_scope *s = ctx->defers; s && s != boundary; s = s->parent) {
        if (s->defers.size > INT32_MAX) fatal("overflow");
        for (i32 i = (i32)s->defers.size - 1; i >= 0; i--) {
            cat_open_curlyln(self);
            (void)generate_expr(self, null, s->defers.v[i], ctx);
            cat_close_curlyln(self);
        }
    }
    ctx->is_effective_void = save_is_effective_void;
}

static str generate_body(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    int has_defers = node->body.defers.size > 0;

    // Push defer scope if this body has defers
    defer_scope scope;
    if (has_defers) {
        scope       = (defer_scope){.defers = node->body.defers, .parent = ctx->defers};
        ctx->defers = &scope;
    }

    str out = str_empty();
    forall(i, node->body.expressions) {
        out = generate_expr(self, null, node->body.expressions.v[i], ctx);
    }

    // Capture result before running defers (defers could modify the variable that out refers to)
    if (has_defers && !str_is_empty(out) && type && should_assign_result(ctx, type)) {
        str tmp = next_res(self);
        generate_decl(self, tmp, type);
        generate_assign(self, tmp, out);
        out = tmp;
    }

    // Emit defers in reverse order (normal exit path)
    if (has_defers) {
        int save_is_effective_void = ctx->is_effective_void;
        if (node->body.defers.size > INT32_MAX) fatal("overflow");
        for (i32 i = (i32)node->body.defers.size - 1; i >= 0; i--) {
            cat_open_curlyln(self);
            (void)generate_expr(self, null, node->body.defers.v[i], ctx);
            cat_close_curlyln(self);
        }
        ctx->is_effective_void = save_is_effective_void;
    }

    // Pop defer scope
    if (has_defers) {
        ctx->defers = scope.parent;
    }

    return out;
}

// Look up the tag and/or union field types from a tagged union wrapper type.
// Either out pointer may be null if that field is not needed.
static void tagged_union_wrapper_fields(tl_monotype *wrapper_type, tl_monotype **out_tag_type,
                                        tl_monotype **out_union_type) {
    str_sized field_names = wrapper_type->cons_inst->def->field_names;
    forall(f, field_names) {
        if (out_tag_type && str_eq(field_names.v[f], S(AST_TAGGED_UNION_TAG_FIELD)))
            *out_tag_type = wrapper_type->cons_inst->args.v[f];
        if (out_union_type && str_eq(field_names.v[f], S(AST_TAGGED_UNION_UNION_FIELD)))
            *out_union_type = wrapper_type->cons_inst->args.v[f];
    }
}

// Generate case expression for tagged union pattern matching
// case s: Shape { c: Circle { ... } sq: Square { ... } }
// Generates:
//   if (s.tag == Foo__ShapeTag__Circle) { Foo_Circle c = s.u.Circle; ... }
//   else if (s.tag == Foo__ShapeTag__Square) { ... }
static str generate_tagged_union_case(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(node->case_.is_union);
    int is_pointer = node->case_.is_union == AST_TAGGED_UNION_MUTABLE;

    // Generate the expression being matched.
    // For mutable (.&) mode, generate as l-value to avoid copying into a temporary,
    // so that &expr.u.Variant points to the original data, not a copy.
    int save_lvalue = 0;
    if (is_pointer && ctx) {
        save_lvalue      = ctx->want_lvalue;
        ctx->want_lvalue = 1;
    }
    str expr_str = generate_expr(self, null, node->case_.expression, ctx);
    if (is_pointer && ctx) {
        ctx->want_lvalue = save_lvalue;
    }
    // Wrap non-symbol l-value expressions in parentheses for correct precedence
    // e.g., (*p).tag instead of *p.tag
    if (is_pointer && !ast_node_is_symbol(node->case_.expression)) {
        expr_str = str_cat_3(self->transient, S("("), expr_str, S(")"));
    }

    // Get the wrapper type (Shape), looking through Const wrapper
    tl_monotype *wrapper_type = tl_monotype_strip_const(node->case_.expression->type->type);
    if (!tl_monotype_is_inst(wrapper_type))
        exit_error(node->file, node->line, "expected tagged union type in case expression");

    str wrapper_name = wrapper_type->cons_inst->def->name;
    (void)wrapper_name; // may be used for debug

    // Result variable for the entire case expression
    str          res         = str_empty();
    str          end_label   = str_empty();
    tl_monotype *result_type = null;

    if (node->case_.arms.size > 0) {
        ast_node const *first_arm = node->case_.arms.v[0];
        result_type               = first_arm->type->type;
        if (!is_nil_result(result_type)) {
            res       = next_res(self);
            end_label = next_label(self);
            generate_decl(self, res, result_type);
        }
    }

    forall(i, node->case_.arms) {
        if (ast_node_is_nil_or_void(node->case_.conditions.v[i])) {
            // else arm
            if (i + 1 != node->case_.arms.size) {
                exit_error(node->file, node->line, "'else' must be the last branch in a case expression");
            }
            if (i > 0) {
                cat(self, S("else {\n"));
            }

            // Generate else binding if present: OtherVariant name = expr.u.OtherVariant;
            if (node->case_.else_binding && ast_node_is_symbol(node->case_.else_binding) &&
                node->case_.else_binding->symbol.annotation_type) {
                tl_monotype *eb_type            = node->case_.else_binding->symbol.annotation_type->type;

                str          other_variant_name = str_empty();
                if (i > 0 && node->case_.conditions.size > 0) {
                    ast_node *primary_cond = node->case_.conditions.v[0];
                    if (ast_node_is_symbol(primary_cond) && primary_cond->symbol.annotation) {
                        str pname = ast_node_name_original(primary_cond->symbol.annotation);
                        tagged_union_other_variant(wrapper_type, pname, &other_variant_name);
                    }
                }
                if (!str_is_empty(other_variant_name)) {
                    str eb_binding_name =
                      escape_c_keyword(self->transient, ast_node_str(node->case_.else_binding));

                    generate_decl(self, eb_binding_name, eb_type);
                    cat(self, eb_binding_name);
                    cat(self, S(" = "));
                    if (is_pointer) cat(self, S("&"));
                    cat(self, expr_str);
                    cat(self, S(".u."));
                    cat(self, escape_c_keyword(self->transient, other_variant_name));
                    cat_semicolonln(self);
                }
            }

            str arm_body = generate_expr(self, null, node->case_.arms.v[i], ctx);
            if (result_type && should_assign_result(ctx, result_type)) {
                generate_assign(self, res, arm_body);
            } else if (!str_is_empty(arm_body)) {
                cat(self, arm_body);
                cat_semicolonln(self);
            }

            if (!str_is_empty(end_label)) {
                cat(self, S("goto "));
                cat(self, end_label);
                cat_semicolonln(self);
            }
            if (i > 0) {
                cat_close_curlyln(self);
            }
            break;
        }

        ast_node *cond = node->case_.conditions.v[i];
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) {
            exit_error(cond->file, cond->line,
                       "tagged union case condition must be 'binding: VariantType'");
        }

        // Get variant type from the condition's annotation
        ast_node    *variant_type_node = cond->symbol.annotation;
        tl_monotype *variant_type      = cond->symbol.annotation_type->type;

        if (!tl_monotype_is_inst(variant_type))
            exit_error(cond->file, cond->line, "expected variant type in case condition");

        // Get the variant name (unmangled) from the annotation
        str variant_name = ast_node_name_original(variant_type_node);

        // Get the tag type from the wrapper struct
        tl_monotype *tag_type = null;
        tagged_union_wrapper_fields(wrapper_type, &tag_type, null);
        if (!tag_type) fatal("wrapper type missing 'tag' field");
        str tag_enum_name = tag_type->cons_inst->def->name;

        // Build tag value name: TagEnumName_VariantName
        // e.g., Foo__ShapeTag_ + "_" + Circle -> Foo__ShapeTag__Circle
        str tag_value_name = str_qualify(self->transient, tag_enum_name, variant_name);

        // Generate: if (expr.tag == TagEnumValue) {
        if (i == 0) {
            cat(self, S("if ("));
        } else {
            cat(self, S("else if ("));
        }
        cat(self, expr_str);
        cat(self, S(".tag == "));
        cat(self, tag_value_name);
        cat(self, S(") {\n"));

        // Generate binding: VariantType binding = expr.u.VariantName;
        str binding_name = escape_c_keyword(self->transient, ast_node_str(cond));
        generate_decl(self, binding_name, variant_type);
        cat(self, binding_name);
        cat(self, S(" = "));
        if (is_pointer) cat(self, S("&"));
        cat(self, expr_str);
        cat(self, S(".u."));
        cat(self, escape_c_keyword(self->transient, variant_name));
        cat_semicolonln(self);

        // Generate arm body
        str arm_body = generate_expr(self, null, node->case_.arms.v[i], ctx);
        if (result_type && should_assign_result(ctx, result_type)) {
            generate_assign(self, res, arm_body);
        } else if (!str_is_empty(arm_body)) {
            cat(self, arm_body);
            cat_semicolonln(self);
        }

        if (!str_is_empty(end_label)) {
            cat(self, S("goto "));
            cat(self, end_label);
            cat_semicolonln(self);
        }

        cat_close_curlyln(self);
    }

    if (!str_is_empty(end_label)) {
        cat_nl(self);
        cat(self, end_label);
        cat(self, S(":"));
        cat_semicolonln(self);
    }

    if (ctx && !str_is_empty(res)) ctx->is_effective_void = 0;
    return res;
}

static str generate_case(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;
    assert(ast_case == node->tag);
    if (node->case_.conditions.size != node->case_.arms.size) fatal("logic error");

    // Handle tagged union case expressions
    if (node->case_.is_union) {
        return generate_tagged_union_case(self, node, ctx);
    }

    ast_node    *bin_pred   = null;
    ast_node    *lfa_args[] = {null, null}; // ignored because we generate args manually
    ast_node    *lfa        = null;
    str_array    args_res   = {.alloc = self->transient};
    ast_node    *nfa        = null;
    tl_monotype *bool_type  = tl_type_registry_bool(self->registry);
    if (node->case_.binary_predicate) {
        bin_pred = node->case_.binary_predicate;
        if (!ast_node_is_symbol(bin_pred) && !ast_node_is_lambda_function(bin_pred)) fatal("logic error");

        if (ast_node_is_lambda_function(bin_pred)) {
            // if predicate is a lambda function, construct an anon lambda application node for
            // convenience
            lfa =
              ast_node_create_lfa(self->transient, bin_pred, (ast_node_sized){.size = 2, .v = lfa_args});

        } else {
            // predicate is an identifier, wrap it in a named function application
            nfa = ast_node_create_nfa(self->transient, bin_pred, (ast_node_sized){0},
                                      (ast_node_sized){.size = 2, .v = lfa_args});
        }

        // allocate room for conditional arm arguments
        array_reserve(args_res, 2);
        // clang-format off
        { str _t = str_empty(); array_push(args_res, _t); }
        { str _t = str_empty(); array_push(args_res, _t); }
        // clang-format on
    }

    switch (node->case_.conditions.size) {
    case 0:
        //
        (void)generate_expr(self, null, node->case_.expression, ctx);
        return str_empty();

    default: {
        ast_node const *cond        = node->case_.expression;
        ast_node const *yes         = node->case_.arms.v[0];
        tl_monotype    *result_type = yes->type->type;

        str             cond_str    = generate_expr(self, null, cond, ctx);
        str             res         = next_res(self);
        str             end_label   = next_label(self);

        generate_decl(self, res, result_type);

        forall(i, node->case_.arms) {
            if (ast_node_is_nil_or_void(node->case_.conditions.v[i])) {
                // the else case: must be last
                if (i + 1 != node->case_.arms.size) fatal("logic error");

                str arm_body = generate_expr(self, null, node->case_.arms.v[i], ctx);
                if (should_assign_result(ctx, result_type)) {
                    generate_assign(self, res, arm_body);
                }
                cat(self, S("goto "));
                cat(self, end_label);
                cat_semicolonln(self);
                break;
            }

            str arm_cond = generate_expr(self, null, node->case_.conditions.v[i], ctx);

            if (node->case_.binary_predicate) {
                if (lfa) {
                    args_res.v[0] = cond_str;
                    args_res.v[1] = arm_cond;
                    str cmp_res   = generate_inline_lambda_with_args(self, bool_type, lfa, ctx, args_res);
                    cat(self, S("if ("));
                    cat(self, cmp_res);
                    cat(self, S(") {\n"));
                } else {
                    assert(nfa);

                    args_res.v[0] = cond_str;
                    args_res.v[1] = arm_cond;
                    str cmp_res   = generate_funcall_with_args(self, nfa, ctx, args_res);
                    cat(self, S("if ("));
                    cat(self, cmp_res);
                    cat(self, S(") {\n"));
                }
            } else {
                cat(self, S("if ("));
                cat(self, cond_str);
                cat(self, S(" == "));
                cat(self, arm_cond);
                cat(self, S(") {\n"));
            }

            str arm_body = generate_expr(self, null, node->case_.arms.v[i], ctx);
            if (should_assign_result(ctx, result_type)) {
                generate_assign(self, res, arm_body);
            }
            cat(self, S("goto "));
            cat(self, end_label);
            cat_semicolonln(self);

            cat_close_curlyln(self);
        }

        cat_nl(self);
        cat(self, end_label);
        cat(self, S(":"));
        cat_semicolonln(self);

        if (ctx && !is_nil_result(result_type)) ctx->is_effective_void = 0;
        return res;

    } break;
    }
}

// Short-circuit evaluation for || and && operators.
// For ||: if left is true, skip evaluating right and return 1.
// For &&: if left is false, skip evaluating right and return 0.
static str generate_short_circuit_op(transpile *self, tl_monotype *type, ast_node const *node,
                                     eval_ctx *ctx, int is_or) {
    // 1. Evaluate left operand
    str left = generate_expr(self, null, node->binary_op.left, ctx);

    // 2. Declare result variable
    str res = next_res(self);
    generate_decl(self, res, type);

    // 3. Generate if statement
    cat(self, S("if ("));
    if (is_or) {
        // For ||: if left is true, short-circuit
        cat(self, left);
    } else {
        // For &&: if left is false (i.e. !left is true), short-circuit
        cat(self, S("!"));
        cat(self, left);
    }
    cat(self, S(") {\n"));

    // 4a. Short-circuit branch (left determined the result)
    if (is_or) {
        // For ||: if left is true, result is 1
        generate_assign(self, res, S("1"));
    } else {
        // For &&: if left is false, result is 0
        generate_assign(self, res, S("0"));
    }

    cat(self, S("} else {\n"));

    // 4b. Non-short-circuit branch (evaluate right)
    str right = generate_expr(self, null, node->binary_op.right, ctx);
    if (should_assign_result(ctx, type)) {
        generate_assign(self, res, right);
    } else {
        cat(self, right);
        cat_semicolonln(self);
    }

    cat(self, S("}\n"));

    return res;
}

static str generate_binary_op(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    assert(ast_binary_op == node->tag);
    str op = ast_node_str(node->binary_op.op);

    // Note: Special case enum field access to mangle the name rather than use a . field access
    // operator.
    if (ast_node_is_symbol(node->binary_op.left) && ast_node_is_symbol(node->binary_op.right) &&
        is_dot_operator(str_cstr(&op))) {
        str          name      = ast_node_str(node->binary_op.left);
        tl_monotype *left_type = env_lookup(self, name);
        if (!left_type) {
            // Handle type alias of an enum
            tl_polytype *registry_type = tl_type_registry_get(self->registry, name);
            if (registry_type && tl_monotype_is_enum(registry_type->type)) {
                name      = registry_type->type->cons_inst->def->generic_name;
                left_type = registry_type->type;
            }
        }

        if (left_type && tl_monotype_is_enum(left_type)) {
            str mangled = str_qualify(self->transient, name, ast_node_str(node->binary_op.right));
            return mangled;
        }
    }

    // Short-circuit logical operators
    if (0 == str_cmp_c(op, "||")) {
        return generate_short_circuit_op(self, type, node, ctx, 1);
    }
    if (0 == str_cmp_c(op, "&&")) {
        return generate_short_circuit_op(self, type, node, ctx, 0);
    }

    // When accessing a CArray struct field, generate the left operand as an lvalue to avoid copying
    // the struct into a temporary. Otherwise the CArray decays to a pointer into the dead temporary.
    int is_struct_access = is_struct_access_operator(str_cstr(&op));
    int carray_field     = is_struct_access && node->binary_op.right->type &&
                       tl_monotype_is_carray(node->binary_op.right->type->type);

    int save_lvalue = ctx->want_lvalue;
    if (carray_field) ctx->want_lvalue = 1;
    str left         = generate_expr(self, null, node->binary_op.left, ctx);
    ctx->want_lvalue = save_lvalue;
    str right;

    // Note: special case if right hand is a funcall of a struct member
    if (ast_node_is_nfa(node->binary_op.right) && is_struct_access) {
        // To handle obj.fun() and obj->fun(), we first load the function pointer from the field `fun`,
        // then invoke the funcall logic. The named_application.name node holds the function type.

        int save_sf          = ctx->is_struct_field;
        ctx->is_struct_field = 1;
        str fun = generate_expr(self, null, node->binary_op.right->named_application.name, ctx);
        ctx->is_struct_field  = save_sf;
        tl_monotype *fun_type = node->binary_op.right->named_application.name->type->type;

        str          fun_res  = next_res(self);
        if (!is_nil_result(fun_type)) {
            generate_decl(self, fun_res, fun_type);
            generate_assign_lhs(self, fun_res);
        }
        cat(self, left);
        cat_op(self, op);
        cat(self, fun);
        cat_semicolonln(self);

        {
            // Note: duplicated with generate_funcall
            node              = node->binary_op.right;
            str          name = fun_res;
            tl_monotype *type = fun_type;

            // type constructor?
            if (tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

            // generate arguments: an array of variables will hold their values
            ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
            str_array      args_res = generate_args(self, args, type, ctx);

            // declare variable to hold funcall result if it's not nil
            str res = generate_funcall_result(self, type);

            assert(tl_monotype_is_list(type));

            // Indirect call through tl_closure: cast fn and pass ctx
            if (!str_is_empty(res) && !is_nil_result(type)) generate_assign_lhs(self, res);
            generate_indirect_closure_call(self, name, type, args_res);
            cat_semicolonln(self);

            return res;
        }

    } else {
        int save_sf = ctx->is_struct_field;
        if (is_struct_access) ctx->is_struct_field = 1;
        right                = generate_expr(self, null, node->binary_op.right, ctx);
        ctx->is_struct_field = save_sf;
    }

    if (!ctx->want_lvalue) {
        str res      = next_res(self);
        int is_index = is_index_operator(str_cstr(&op));

        // Indexing a CArray yields another CArray, and C forbids assigning array
        // types with =. Use a combined decl+init so the type decays to T*.
        if (is_index && !is_nil_result(type) && tl_monotype_is_carray(type)) {
            generate_decl_init(self, res, type, str_cat_4(self->transient, left, S("["), right, S("]")));
            return res;
        }

        if (!is_nil_result(type)) {
            generate_decl(self, res, type);
            generate_assign_lhs(self, res);
        }

        int is_ptr_cmp = !is_index && is_relational_operator(str_cstr(&op)) && node->binary_op.left->type &&
                         node->binary_op.right->type &&
                         tl_monotype_is_ptr(node->binary_op.left->type->type) &&
                         tl_monotype_is_ptr(node->binary_op.right->type->type);

        // Closure-null comparison: compare .fn field to NULL
        int left_is_arrow =
          node->binary_op.left->type && tl_monotype_is_arrow(node->binary_op.left->type->type);
        int right_is_nil = ast_nil == node->binary_op.right->tag;
        int left_is_nil  = ast_nil == node->binary_op.left->tag;
        int right_is_arrow =
          node->binary_op.right->type && tl_monotype_is_arrow(node->binary_op.right->type->type);
        int closure_null_cmp = (left_is_arrow && right_is_nil) || (left_is_nil && right_is_arrow);

        if (is_ptr_cmp) {
            cat(self, S("(void*)"));
        }
        cat(self, left);
        if (closure_null_cmp && left_is_arrow) cat(self, S(".fn"));

        if (is_index) {
            cat(self, S("["));
            cat(self, right);
            cat_close_square(self);
        } else {
            cat_op(self, op);
            if (is_ptr_cmp) {
                cat(self, S("(void*)"));
            }
            cat(self, right);
            if (closure_null_cmp && right_is_arrow) cat(self, S(".fn"));
        }
        cat_semicolonln(self);
        return res;
    }

    else {
        str_build b = str_build_init(self->transient, 64);
        str_build_cat(&b, left);
        int is_index = is_index_operator(str_cstr(&op));
        if (is_index) {
            str_build_cat(&b, S("["));
            str_build_cat(&b, right);
            str_build_cat(&b, S("]"));
        } else {
            str_build_cat(&b, op);
            str_build_cat(&b, right);
        }
        return str_build_finish(&b);
    }
}

static str generate_unary_op(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    assert(ast_unary_op == node->tag);
    str op = ast_node_str(node->unary_op.op);

    // Note: special case: the address-of operator is special because its operand must not be evaluated
    // in the usual way.
    if (str_eq(op, S("&"))) {
        str res  = next_res(self);

        int save = 0;
        if (ctx) {
            save             = ctx->want_lvalue;
            ctx->want_lvalue = 1;
        }
        str operand = generate_expr(self, null, node->unary_op.operand, ctx);
        if (ctx) {
            ctx->want_lvalue = save;
        }

        generate_decl(self, res, type);
        generate_assign_lhs(self, res);
        cat(self, op);
        cat(self, operand);
        cat_semicolonln(self);
        return res;
    }

    str operand = generate_expr(self, null, node->unary_op.operand, ctx);

    if (!ctx || !ctx->want_lvalue) {
        str res = next_res(self);
        if (!is_nil_result(type)) {
            generate_decl(self, res, type);
            generate_assign_lhs(self, res);
        }
        cat(self, op);
        cat(self, operand);
        cat_semicolonln(self);
        return res;
    } else {
        str_build b = str_build_init(self->transient, 64);
        str_build_cat(&b, op);
        str_build_cat(&b, operand);
        return str_build_finish(&b);
    }
}

static str generate_assignment(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {

    str value = generate_expr(self, type, node->assignment.value, ctx);

    // Field name assignments (e.g., in struct construction: Foo(bar = val)) should not emit
    // the assignment statement - they are just named arguments whose values get used.
    if (node->assignment.is_field_name) return value;

    int save         = ctx->want_lvalue;
    ctx->want_lvalue = 1;
    str lhs          = generate_expr(self, null, node->assignment.name, ctx);
    ctx->want_lvalue = save;

    str op           = node->assignment.op ? ast_node_str(node->assignment.op) : str_empty();
    if (!is_nil_result(type)) generate_assign_op(self, lhs, value, op);

    // Note: if this is an assignment by operation (+=, -=, etc), the value of the expression is the result.
    // Otherwise, the value of the expression is the right hand side.
    if (node->assignment.op) return lhs;
    else return value;
}

static void generate_reassignment(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {

    str value = generate_expr(self, type, node->assignment.value, ctx);

    // Field name assignments (e.g., in struct construction: Foo(bar = val)) should not emit
    // the assignment statement - they are just named arguments whose values get used.
    if (node->assignment.is_field_name) return;

    int save         = ctx->want_lvalue;
    ctx->want_lvalue = 1;
    str lhs          = generate_expr(self, null, node->assignment.name, ctx);
    ctx->want_lvalue = save;

    str op           = node->assignment.op ? ast_node_str(node->assignment.op) : str_empty();
    if (!is_nil_result(type)) generate_assign_op(self, lhs, value, op);
}

static str generate_try(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;

    // Generate the operand expression (the tagged union value)
    str operand_val = generate_expr(self, null, node->try_.operand, ctx);

    // Get the wrapper type (the tagged union struct)
    tl_monotype *wrapper_type = node->try_.operand->type->type;

    // Find tag and union fields
    tl_monotype *tag_type   = null;
    tl_monotype *union_type = null;
    tagged_union_wrapper_fields(wrapper_type, &tag_type, &union_type);
    if (!tag_type || !union_type)
        exit_error(node->file, node->line,
                   "try expression requires a tagged union with 'tag' and 'union' fields");

    // Get variant names: first = success, second = error
    str_sized variant_names = union_type->cons_inst->def->field_names;
    if (variant_names.size < 2)
        exit_error(node->file, node->line,
                   "try expression requires a tagged union with at least two variants");
    str success_name = variant_names.v[0];
    str error_name   = variant_names.v[1];

    // Build tag value for the error variant
    str tag_enum_name  = tag_type->cons_inst->def->name;
    str tag_error_name = str_qualify(self->transient, tag_enum_name, error_name);

    // Save operand to a temp variable to avoid re-evaluation
    str tmp = next_res(self);
    generate_decl(self, tmp, wrapper_type);
    generate_assign(self, tmp, operand_val);

    // Emit: if (tmp.tag == error_tag) { [defers]; return tmp; }
    cat(self, S("if ("));
    cat(self, tmp);
    cat(self, S(".tag == "));
    cat(self, tag_error_name);
    cat(self, S(") {\n"));

    // Emit defers before early return (same as generate_return)
    emit_defers_until(self, ctx, null);

    // Check if operand type matches function return type (cross-type try support)
    str op_c_type  = type_to_c_mono(self, wrapper_type);
    str ret_c_type = ctx->func_return_type ? type_to_c_mono(self, ctx->func_return_type) : str_empty();

    if (str_is_empty(ret_c_type) || str_eq(op_c_type, ret_c_type)) {
        // Same type — return directly
        cat(self, S("return "));
        cat(self, tmp);
        cat_semicolonln(self);
    } else {
        // Different wrapper type — construct new Result of function return type with error value
        tl_monotype *ret_tag_type   = null;
        tl_monotype *ret_union_type = null;
        tagged_union_wrapper_fields(ctx->func_return_type, &ret_tag_type, &ret_union_type);

        str          ret_error_name   = ret_union_type->cons_inst->def->field_names.v[1];
        str          ret_tag_enum     = ret_tag_type->cons_inst->def->name;
        str          ret_tag_err_name = str_qualify(self->transient, ret_tag_enum, ret_error_name);

        tl_monotype *ret_err_variant  = ret_union_type->cons_inst->args.v[1];
        str_sized    err_fields       = ret_err_variant->cons_inst->def->field_names;

        str          err_res          = next_res(self);
        generate_decl(self, err_res, ctx->func_return_type);

        // Set tag
        generate_assign_field(self, err_res, S(AST_TAGGED_UNION_TAG_FIELD), ret_tag_err_name);

        // Copy error fields: err_res.u.ErrName.field = tmp.u.ErrName.field
        str esc_ret_err = escape_c_keyword(self->transient, ret_error_name);
        str esc_op_err  = escape_c_keyword(self->transient, error_name);
        forall(f, err_fields) {
            str fname = escape_c_keyword(self->transient, err_fields.v[f]);
            cat(self, err_res);
            cat(self, S(".u."));
            cat(self, esc_ret_err);
            cat_dot(self);
            cat(self, fname);
            cat_assign(self);
            cat(self, tmp);
            cat(self, S(".u."));
            cat(self, esc_op_err);
            cat_dot(self);
            cat(self, fname);
            cat_semicolonln(self);
        }

        cat(self, S("return "));
        cat(self, err_res);
        cat_semicolonln(self);
    }
    cat(self, S("}\n"));

    // Unwrap: result = tmp.u.success_name.field_name (full unwrap to inner value)
    tl_monotype *success_variant_type = union_type->cons_inst->args.v[0];
    str          field_name           = success_variant_type->cons_inst->def->field_names.v[0];

    str          res                  = next_res(self);
    tl_monotype *success_type         = node->type->type;
    generate_decl(self, res, success_type);
    generate_assign_lhs(self, res);
    cat(self, tmp);
    cat(self, S(".u."));
    cat(self, escape_c_keyword(self->transient, success_name));
    cat(self, S("."));
    cat(self, escape_c_keyword(self->transient, field_name));
    cat_semicolonln(self);

    return res;
}

static str generate_return(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    // Note: handles return [expr] and break

    int has_value = !!node->return_.value;
    int is_break  = node->return_.is_break_statement;

    str value     = str_empty();
    if (has_value) value = generate_expr(self, type, node->return_.value, ctx);

    // Capture return value to a temp before running defers
    if (!is_break && has_value && ctx->defers) {
        tl_monotype *val_type = node->return_.value->type->type;
        if (!is_nil_result(val_type)) {
            str tmp = next_res(self);
            generate_decl(self, tmp, val_type);
            generate_assign(self, tmp, value);
            value = tmp;
        }
    }

    // Emit defers for scopes being exited
    if (is_break) emit_defers_until(self, ctx, ctx->loop_defer_boundary);
    else emit_defers_until(self, ctx, null);

    if (is_break) cat(self, S("break"));
    else cat(self, S("return"));

    if (has_value && !is_break) {
        cat_sp(self);
        cat(self, value);
    }

    cat_semicolonln(self);
    return value;
}

static str generate_while(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;

    // due to the stack-based transpiler, we rewrite while statement as follows:
    // while(1) { if (!condition) break; body; update_label: update}

    cat(self, S("while(1) "));
    cat_open_curlyln(self);

    str condition = generate_expr(self, null, node->while_.condition, ctx);
    cat(self, S("if"));
    cat_open_round(self);
    cat(self, S("!"));
    cat_open_round(self);
    cat(self, condition);
    cat_close_round(self);
    cat_close_round(self);
    cat(self, S("break;\n"));

    str save_update_label                 = ctx->update_label;
    ctx->update_label                     = next_label(self);

    defer_scope *save_loop_defer_boundary = ctx->loop_defer_boundary;
    ctx->loop_defer_boundary              = ctx->defers;

    (void)generate_expr(self, null, node->while_.body, ctx);

    ctx->loop_defer_boundary = save_loop_defer_boundary;

    // include semicolon after label for c99 reasons: label followed by a declaration is a c23 thing
    cat(self, ctx->update_label);
    cat(self, S(":;\n"));
    ctx->update_label = save_update_label;

    if (node->while_.update) (void)generate_expr(self, null, node->while_.update, ctx);

    cat_close_curlyln(self);

    return str_empty();
}

static str generate_continue(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;
    (void)node;

    if (str_is_empty(ctx->update_label)) fatal("logic error");

    emit_defers_until(self, ctx, ctx->loop_defer_boundary);

    cat(self, S("goto "));
    cat(self, ctx->update_label);
    cat_semicolonln(self);
    return str_empty();
}

static str generate_void_else(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    (void)type;
    assert(ast_void_else == node->tag);
    ast_node const *expr    = node->void_else.expression;
    ast_node const *binding = node->void_else.else_binding;
    ast_node const *body    = node->void_else.else_body;

    // Get wrapper type from the expression
    tl_monotype *wrapper_type = expr->type->type;

    // Find tag and union fields
    tl_monotype *tag_type   = null;
    tl_monotype *union_type = null;
    tagged_union_wrapper_fields(wrapper_type, &tag_type, &union_type);
    if (!tag_type || !union_type)
        exit_error(node->file, node->line,
                   "void-else expression requires a tagged union with 'tag' and 'union' fields");

    // Get variant names: first = success, second = error
    str_sized variant_names = union_type->cons_inst->def->field_names;
    str       success_name  = variant_names.v[0];
    str       error_name    = variant_names.v[1];

    // Build tag value for success variant
    str tag_enum_name    = tag_type->cons_inst->def->name;
    str tag_success_name = str_qualify(self->transient, tag_enum_name, success_name);

    // Emit scoped block
    cat_open_curlyln(self);

    // Generate expression and save to temp
    str expr_str = generate_expr(self, null, expr, ctx);
    str tmp      = next_res(self);
    generate_decl(self, tmp, wrapper_type);
    generate_assign(self, tmp, expr_str);

    // if (tmp.tag != SuccessTag) { ErrorVariant binding = tmp.u.ErrorName; body }
    cat(self, S("if ("));
    cat(self, tmp);
    cat(self, S(".tag != "));
    cat(self, tag_success_name);
    cat(self, S(") {\n"));

    // Declare and assign else binding
    tl_monotype *eb_type = binding->symbol.annotation_type->type;
    str          eb_name = escape_c_keyword(self->transient, ast_node_str(binding));
    generate_decl(self, eb_name, eb_type);
    cat(self, eb_name);
    cat(self, S(" = "));
    cat(self, tmp);
    cat(self, S(".u."));
    cat(self, escape_c_keyword(self->transient, error_name));
    cat_semicolonln(self);

    // Generate else body
    generate_expr(self, null, body, ctx);

    cat(self, S("}\n"));
    cat_close_curlyln(self);

    return str_empty();
}

static str generate_expr(transpile *self, tl_monotype *type, ast_node const *node, eval_ctx *ctx) {
    // This function is used to generate output to evaluate an expression with a given type, for example
    // for function arguments. If type is null, then the type is taken from the expression. The str
    // returned is the name of the variable which holds the evaluated value.

    if (ctx) ctx->is_effective_void = 0;

    if (!type) {
        assert(node->type);
        type = node->type->type;
    }

    // emit #line directive
    emit_line_directive(self, node, ctx);

    switch (node->tag) {
    case ast_named_function_application:  return generate_funcall(self, node, ctx);
    case ast_lambda_function_application: return generate_inline_lambda(self, type, node, ctx);
    case ast_let_in:                      return generate_let_in(self, type, node, ctx);
    case ast_i64:                         return generate_str(self, str_init_i64(self->transient, node->i64.val), type);
    case ast_i64_z:                       return generate_str(self, str_init_i64(self->transient, node->i64_z.val), type);
    case ast_u64:
        return generate_str(
          self, str_cat(self->transient, str_init_u64(self->transient, node->u64.val), S("ULL")), type);
    case ast_u64_zu:
        return generate_str(
          self, str_cat(self->transient, str_init_u64(self->transient, node->u64_zu.val), S("ULL")), type);
    case ast_f64:  return generate_str(self, str_init_f64(self->transient, node->f64.val), type);
    case ast_bool: return generate_str(self, node->bool_.val ? S("1 /*true*/") : S("0 /*false*/"), type);
    case ast_char:
        return generate_str(self, str_cat_3(self->transient, S("'"), node->symbol.name, S("'")), type);
    case ast_string:
        return generate_str(self, str_cat_3(self->transient, S("\""), node->symbol.name, S("\"")), type);

    case ast_symbol: {
        // For symbols, it's type in the environment is the most correct, versus its type in the ast.
        // Override any provided type with the environment type, if it exists.
        str          name     = ast_node_str(node);
        tl_monotype *env_type = env_lookup(self, name);
        if (env_type) type = env_type;

        // Bare function name without /N arity suffix: an arrow-typed symbol that isn't a tl_-prefixed
        // local variable, a c_ FFI symbol, or a struct field access. This will emit a bare C
        // identifier that doesn't resolve.
        if (type && tl_monotype_is_arrow(type) && !(ctx && ctx->is_struct_field) &&
            0 != str_cmp_nc(name, "tl_", 3) && !is_c_symbol(name) && !str_contains(name, S("__"))) {
            u32 arity   = type->list.xs.v[0]->list.xs.size;
            str display = ast_node_name_original(node);
            if (str_is_empty(display)) display = name;
            exit_error(node->file, node->line,
                       "'%s' is a function — to create a function pointer, use '%s/%u'", str_cstr(&display),
                       str_cstr(&display), arity);
        }

        return generate_expr_symbol(self, type, name, ast_node_name_original(node), ctx);
    }

    case ast_if_then_else: return generate_if_then_else(self, node, ctx);

    case ast_return:       return generate_return(self, type, node, ctx);
    case ast_try:          return generate_try(self, type, node, ctx);
    case ast_while:        return generate_while(self, type, node, ctx);
    case ast_void_else:    return generate_void_else(self, type, node, ctx);
    case ast_continue:     return generate_continue(self, type, node, ctx);

    case ast_nil:
        if (type && tl_monotype_is_arrow(type)) return S("(tl_closure){0}");
        return S("NULL");
    case ast_void:            return str_empty();

    case ast_tuple:           return generate_tuple(self, type, node, ctx);

    case ast_body:            return generate_body(self, type, node, ctx);
    case ast_case:            return generate_case(self, type, node, ctx);

    case ast_binary_op:       return generate_binary_op(self, type, node, ctx);
    case ast_unary_op:        return generate_unary_op(self, type, node, ctx);

    case ast_assignment:      return generate_assignment(self, type, node, ctx);

    case ast_reassignment:
    case ast_reassignment_op: generate_reassignment(self, type, node, ctx); return str_empty();

    case ast_arrow:
    case ast_ellipsis:
    case ast_eof:
    case ast_trait_definition:
    case ast_user_type_definition:
    case ast_lambda_function:
    case ast_let:
        cat_commentln(self, S("FIXME: generate_expr"));
        return str_copy(self->transient, S("FIXME_generate_expr"));
        break;

    case ast_hash_command:
    case ast_type_alias:   fatal("logic error");

    case ast_type_predicate:
        return generate_str(self,
                            node->type_predicate.is_valid ? S("1 /*type_predicate_valid*/")
                                                          : S("0 /*type_predicate_invalid*/"),
                            type);

    case ast_attribute_set:
        // ignored
        break;
    }
    fatal("unreachable");
}

// Returns 0 if type doesn't contain an arrow, otherwise returns
// the number of Ptr wrappers (for nested pointer stars)
static int ptr_depth_to_arrow(tl_monotype *type, tl_monotype **out_arrow) {
    int depth = 0;
    while (tl_monotype_is_ptr(type)) {
        depth++;
        type = tl_monotype_ptr_target(type);
    }
    // Skip Const wrapper (e.g. Ptr(Const(Arrow(...))))
    type = tl_monotype_strip_const(type);
    if (tl_monotype_is_arrow(type)) {
        if (out_arrow) *out_arrow = type;
        return depth;
    }
    return 0;
}

static void build_arrow_to_c(transpile *, str_build *b, tl_monotype *type, str name);

// Returns C type string for Ptr(..Arrow..) types, e.g. "int (**)(int)"
// Returns empty string if type is not Ptr-to-arrow.
static str ptr_to_arrow_to_c(transpile *self, tl_monotype *type) {
    tl_monotype *arrow     = null;
    int          ptr_depth = ptr_depth_to_arrow(type, &arrow);
    if (ptr_depth == 0) return str_empty();

    // Build stars string for the pointer depth
    str_build stars_b = str_build_init(self->transient, ptr_depth + 1);
    for (int i = 0; i < ptr_depth; i++) {
        str_build_cat(&stars_b, S("*"));
    }
    str       stars = str_build_finish(&stars_b);

    str_build b     = str_build_init(self->transient, 64);
    build_arrow_to_c(self, &b, arrow, stars);
    return str_build_finish(&b);
}

// Returns C declaration for Ptr(..Arrow..) types with name, e.g. "int (**name)(int)"
// Returns empty string if type is not Ptr-to-arrow.
static str ptr_to_arrow_decl(transpile *self, tl_monotype *type, str name) {
    tl_monotype *arrow     = null;
    int          ptr_depth = ptr_depth_to_arrow(type, &arrow);
    if (ptr_depth == 0) return str_empty();

    // Build "*...*name" string
    str_build stars_b = str_build_init(self->transient, ptr_depth + 8);
    for (int i = 0; i < ptr_depth; i++) {
        str_build_cat(&stars_b, S("*"));
    }
    str_build_cat(&stars_b, name);
    str       name_with_stars = str_build_finish(&stars_b);

    str_build b               = str_build_init(self->transient, 80);
    build_arrow_to_c(self, &b, arrow, name_with_stars);
    return str_build_finish(&b);
}

static void generate_decl(transpile *self, str name, tl_monotype *type) {
    // Strip Const wrapper: generate_decl emits a declaration without initializer,
    // and C requires const variables to be initialized at declaration. Callers that
    // need const use generate_decl_init instead.
    type = tl_monotype_strip_const(type);
    name = escape_c_keyword(self->transient, name);
    if (tl_arrow == type->tag) {
        // arrow

        str_build b = str_build_init(self->transient, 80);
        build_arrow_to_c(self, &b, type, name);
        cat(self, str_build_finish(&b));
        cat_semicolonln(self);

    }

    else if (tl_cons_inst == type->tag) {
        // Special case for CArray(T, N) - emit as T name[N]
        if (tl_monotype_is_carray(type)) {
            str typec = type_to_c_mono(self, tl_monotype_carray_element(type));
            if (tl_monotype_carray_count_is_macro(type)) {
                str macro  = tl_monotype_carray_count_macro_name(type);
                str c_name = remove_c_prefix(self->transient, macro);
                str line   = str_fmt(self->transient, "%s %s[%.*s];\n", str_cstr(&typec), str_cstr(&name),
                                     str_ilen(c_name), str_buf(&c_name));
                cat(self, line);
            } else {
                i32 count = tl_monotype_carray_count(type);
                str line =
                  str_fmt(self->transient, "%s %s[%i];\n", str_cstr(&typec), str_cstr(&name), count);
                cat(self, line);
            }
            return;
        }

        // Special case for Ptr(..Ptr(Arrow)..) - pointer(s) to function pointer
        str ptr_arrow = ptr_to_arrow_decl(self, type, name);
        if (!str_is_empty(ptr_arrow)) {
            cat(self, ptr_arrow);
            cat_semicolonln(self);
            return;
        }

        str typec;
        if (tl_monotype_is_void(type)) {
            typec = S("char");
        } else {
            typec = type_to_c_mono(self, type);
        }

        cat(self, typec);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    }

    else if (tl_tuple == type->tag) {

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    }

    else if (tl_monotype_is_tv(type)) {
        fatal("got a type variable");
    }

    else if (tl_monotype_is_weak(type)) {
        fatal("got a weak variable");
    }

    else if (tl_monotype_is_weak_int(type)) {
        fatal("got a weak integer variable");
    }

    else {
        fatal("unexpected type");
    }
}

static void generate_decl_pointer(transpile *self, str name, tl_monotype *type) {
    name = escape_c_keyword(self->transient, name);
    if (tl_arrow == type->tag) {
        // arrow — pointer to tl_closure
        cat(self, S("tl_closure* "));
        cat(self, name);
        cat_semicolonln(self);

    }

    else if (tl_cons_inst == type->tag) {
        if (tl_monotype_is_void(type)) fatal("can't declare a void type");

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_star(self);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);
    }

    else if (tl_tuple == type->tag) {

        str typec = type_to_c_mono(self, type);
        cat(self, typec);
        cat_star(self);
        cat_sp(self);
        cat(self, name);
        cat_semicolonln(self);

    }

    else {
        fatal("got a type variable");
    }
}

int transpile_compile(transpile *self, str_build *out_build) {

    self->build = str_build_init(self->parent, TRANSPILE_BUILD_SIZE);

    // Emit version/date header comment
    {
        time_t     now = time(0);
        struct tm *tm  = localtime(&now);
        char       buf[256];
        int        n = snprintf(buf, sizeof(buf),
                                "/* Generated by Tess compiler %.*s on %04d-%02d-%02d %02d:%02d:%02d */\n",
                                str_ilen(self->opts.version), str_buf(&self->opts.version), tm->tm_year + 1900,
                                tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
        str_build_cat_n(&self->build, buf, (u32)n);
    }

    str_build_cat(&self->build, str_init_static(embed_std_c));
    cat_nl(self);
    cat_nl(self);

    generate_hash_includes(self);

    cat(self, S("typedef struct tl_closure { void* fn; void* ctx; } tl_closure;\n\n"));

    generate_user_types(self);
    generate_structs(self);
    generate_toplevel_contexts(self);

    generate_prototypes(self, 1);
    cat_nl(self);

    // Generate toplevel values and functions into a temporary buffer so that
    // C FFI thunks (discovered lazily during codegen) can be emitted before them.
    str_build saved_build = self->build;
    self->build           = str_build_init(self->parent, TRANSPILE_BUILD_SIZE);

    generate_toplevel_values(self);
    cat_nl(self);

    generate_toplevels(self);
    cat_nl(self);

    // Assemble: thunks first (they reference prototypes above), then toplevel code
    str_build toplevels_build = self->build;
    self->build               = saved_build;

    str thunks_str            = str_build_str(self->transient, self->thunks_build);
    if (!str_is_empty(thunks_str)) {
        cat(self, thunks_str);
        cat_nl(self);
    }

    emit_interned_block(self);
    if (self->interned_offset > 0) cat_nl(self);

    str_build_cat_n(&self->build, toplevels_build.v, toplevels_build.size);

    validate_c_exports(self);

    if (self->opts.is_library) generate_c_exports(self);

    if (!self->opts.is_library) generate_main(self);

    if (out_build) {
        *out_build = self->build;
    }
    return 0;
}

//

transpile *transpile_create(allocator *alloc, transpile_opts const *opts) {
    transpile *self = new(alloc, transpile);

    self->opts      = *opts;

    self->parent    = alloc;
    self->arena     = arena_create(alloc, TRANSPILE_ARENA_SIZE);
    self->transient = arena_create(alloc, TRANSPILE_TRANSIENT_SIZE);

    // Flatten body nodes to get all individual type definitions
    // (body nodes come from tagged union desugaring which produces multiple UTDs)
    ast_node_array flat_nodes = {.alloc = self->arena};
    forall(i, opts->infer_result.nodes) {
        ast_node *node = opts->infer_result.nodes.v[i];
        if (ast_node_is_body(node)) {
            forall(j, node->body.expressions) {
                array_push(flat_nodes, node->body.expressions.v[j]);
            }
        } else {
            array_push(flat_nodes, node);
        }
    }
    self->nodes             = (ast_node_sized)array_sized(flat_nodes);
    self->infer             = opts->infer_result.infer;
    self->registry          = opts->infer_result.registry;
    self->env               = opts->infer_result.env;
    self->subs              = opts->infer_result.subs;
    self->toplevels         = opts->infer_result.toplevels;
    self->specializations   = opts->infer_result.specializations;
    self->synthesized_nodes = opts->infer_result.synthesized_nodes;
    self->hash_includes     = opts->infer_result.hash_includes;

    self->structs           = hset_create(self->arena, 64);
    self->context_generated = hset_create(self->arena, 64);
    self->thunks_generated  = hset_create(self->arena, 64);
    self->thunks_build      = str_build_init(self->arena, 256);

    self->interned_strings  = map_new(self->arena, str, u64, 64);
    self->interned_offset   = 0;

    self->next_res          = 0;
    self->next_block        = 0;

    self->no_line_directive = !!opts->no_line_directive;
    self->verbose           = !!opts->verbose;

    self->toplevels_sorted  = str_map_sorted_keys(self->arena, self->toplevels);

    return self;
}

void transpile_destroy(allocator *alloc, transpile **p) {
    if (!p || !*p) return;

    arena_destroy(&(*p)->transient);
    arena_destroy(&(*p)->arena);
    alloc_free(alloc, *p);
    *p = null;
}

void transpile_set_verbose(transpile *self, int val) {
    self->verbose = val;
}

void transpile_get_arena_stats(transpile *self, arena_stats *main_out, arena_stats *transient_out) {
    arena_get_stats(self->arena, main_out);
    if (transient_out) arena_get_stats(self->transient, transient_out);
}

//

static str next_res(transpile *self) {
    char buf[64];
    int  len = snprintf(buf, sizeof buf, "tl_res%" PRIu64, self->next_res++);
    return str_init_n(self->transient, buf, len);
}

static str next_label(transpile *self) {
    char buf[64];
    int  len = snprintf(buf, sizeof buf, "tl_label%" PRIu64, self->next_block++);
    return str_init_n(self->transient, buf, len);
}

//

static void cat(transpile *self, str s) {
    str_build_cat(&self->build, s);
}
static void cat_nl(transpile *self) {
    cat(self, S("\n"));
}
static void cat_sp(transpile *self) {
    cat(self, S(" "));
}
static void cat_op(transpile *self, str op) {
    cat(self, S(" "));
    cat(self, op);
    cat(self, S(" "));
}
static void cat_ampersand(transpile *self) {
    cat(self, S("&"));
}
static void cat_assign(transpile *self) {
    cat(self, S(" = "));
}
static void cat_commasp(transpile *self) {
    cat(self, S(", "));
}
static void cat_dot(transpile *self) {
    cat(self, S("."));
}
// static void cat_double_slash(transpile *self) {
//     cat(self, S("// "));
// }
static void cat_open_round(transpile *self) {
    cat(self, S("("));
}
static void cat_close_round(transpile *self) {
    cat(self, S(")"));
}
static void cat_open_curly(transpile *self) {
    cat(self, S("{"));
}
static void cat_open_curlyln(transpile *self) {
    cat(self, S("{\n"));
}
static void cat_close_curly(transpile *self) {
    cat(self, S("}"));
}
static void cat_close_square(transpile *self) {
    cat(self, S("]"));
}
static void cat_close_curlyln(transpile *self) {
    cat(self, S("}\n"));
}
static void cat_semicolon(transpile *self) {
    cat(self, S(";"));
}
static void cat_semicolonln(transpile *self) {
    cat(self, S(";\n"));
}
static void cat_star(transpile *self) {
    cat(self, S("*"));
}
static void cat_return(transpile *self, str s) {
    cat(self, S("return "));
    cat(self, s);
    cat(self, S(";\n"));
}
static void catln(transpile *self, str s) {
    cat(self, s);
    cat_nl(self);
}
static void cat_comment(transpile *self, str s) {
    cat(self, S("/* "));
    cat(self, s);
    cat(self, S(" */"));
}
static void cat_commentln(transpile *self, str s) {
    cat_comment(self, s);
    cat_nl(self);
}
// static void cat_i64(transpile *self, i64 val) {
//     str s = str_init_i64(self->transient, val);
//     cat(self, s);
//     str_deinit(self->transient, &s);
// }
// static void cat_f64(transpile *self, f64 val) {
//     str s = str_init_f64(self->transient, val);
//     cat(self, s);
//     str_deinit(self->transient, &s);
// }

//

static str mangle_fun(transpile *self, str s) {
    // If name is already mangled, it could be a variable name. Don't mangle it further.
    if (0 == str_cmp_nc(s, "tl_", 3)) return s;

    // don't mangle names which don't refer to actual functions. This helps avoid mangling struct field
    // names that have an arrow type.
    if (!str_map_contains(self->toplevels, s)) return s;

    str_build b = str_build_init(self->transient, str_len(s) + 7);
    str_build_cat(&b, S("tl_fun_"));
    str_build_cat(&b, s);

    return str_build_finish(&b);
}

static int should_generate(transpile *self, str name, tl_polytype *type) {
    // return 0 if this function should not be generated by transpile
    // during its processing of functions in the environment.

    // generate main even if generic type
    if (is_main_function(name)) return 1;

    // never generate c_ prefixed functions
    if (is_c_symbol(name)) return 0;

    if (tl_polytype_is_scheme(type)) {
        if (self->verbose) cat_commentln(self, str_cat(self->transient, S("scheme: "), name));
        return 0;
    }

    if (!tl_monotype_is_concrete_no_weak(type->type)) {
        if (self->verbose) cat_commentln(self, str_cat(self->transient, S("!concrete: "), name));
        return 0;
    }

    if (!tl_monotype_is_arrow(type->type)) return 0; // not an arrow
    if (is_intrinsic(name)) return 0;

    return 1;
}

static str type_to_c(transpile *self, tl_polytype *type);

static str render_ptr_to_c(transpile *self, tl_monotype *mono) {
    str ptr_arrow = ptr_to_arrow_to_c(self, mono);
    if (!str_is_empty(ptr_arrow)) return ptr_arrow;

    tl_monotype *arg = tl_monotype_ptr_target(mono);
    if (tl_monotype_is_const(arg)) {
        tl_monotype *inner = tl_monotype_const_target(arg);
        tl_polytype  wrap  = tl_polytype_wrap(inner);
        str          typec = type_to_c(self, &wrap);
        return str_cat_3(self->transient, S("const "), typec, S("*"));
    }

    tl_polytype wrap  = tl_polytype_wrap(arg);
    str         typec = type_to_c(self, &wrap);
    return str_cat(self->transient, typec, S("*"));
}

static str type_to_c(transpile *self, tl_polytype *type) {
    if (type->quantifiers.size) fatal("type scheme");
    tl_monotype *mono = type->type;
    if (tl_monotype_is_concrete_inst(mono)) {
        str cons_name = mono->cons_inst->def->name;

        // Data-driven: builtin types have c_type_name set in the type constructor def
        if (!str_is_empty(mono->cons_inst->def->c_type_name)) {
            return mono->cons_inst->def->c_type_name;
        } else if (tl_monotype_is_ptr(mono)) {
            return render_ptr_to_c(self, mono);
        }

        else if (tl_monotype_is_const(mono)) {
            tl_monotype *inner = tl_monotype_const_target(mono);
            tl_polytype  wrap  = tl_polytype_wrap(inner);
            if (tl_monotype_is_ptr(inner)) {
                // Const[Ptr[T]] -> T* const  (or const T* const for Const[Ptr[Const[T]]])
                str ptr_c = render_ptr_to_c(self, inner);
                return str_cat(self->transient, ptr_c, S(" const"));
            }
            // Const[T] -> const T
            str inner_c = type_to_c(self, &wrap);
            return str_cat(self->transient, S("const "), inner_c);
        }

        else if (tl_monotype_is_carray(mono)) {
            // CArray(T, N) renders as T* (decayed pointer)
            tl_monotype *element = tl_monotype_carray_element(mono);
            tl_polytype  wrap_el = tl_polytype_wrap(element);
            str          typec   = type_to_c(self, &wrap_el);
            return str_cat(self->transient, typec, S("*"));
        }

        else {
            if (is_c_symbol(cons_name)) {
                if (is_c_struct_symbol(cons_name))
                    return str_cat(self->transient, S("struct "),
                                   remove_c_struct_prefix(self->transient, cons_name));
                return remove_c_prefix(self->transient, cons_name);
            }

            // Note: special handling of user types. Due to recursive types, we want to use canonical
            // type names. This means we need to do an additional lookup on special_name in the type
            // environment, and use the name of the found type. tl_infer's canonicalize_types ensures
            // that user types are canonicalized.
            str name = mono->cons_inst->special_name;
            if (str_is_empty(name) && mono->cons_inst->args.size > 0) {
                tl_monotype *updated = tl_infer_update_specialized_type(self->infer, mono);
                if (updated && tl_monotype_is_inst(updated) &&
                    !str_is_empty(updated->cons_inst->special_name))
                    name = updated->cons_inst->special_name;
            }
            if (str_is_empty(name)) name = cons_name;

            tl_monotype *found = env_lookup(self, name);
            if (found) {
                if (str_is_empty(found->cons_inst->special_name)) return found->cons_inst->def->name;
                else return found->cons_inst->special_name;
            }
            return name;
        }
    }

    else if (tl_monotype_is_arrow(mono)) {
        // For contexts where we need just the type string (no variable name)
        str_build b = str_build_init(self->transient, 64);
        build_arrow_to_c(self, &b, mono, str_empty());
        return str_build_finish(&b);
    }

    else if (tl_monotype_is_tuple(mono)) {
        str struct_name = make_struct_name(self->transient, mono, null);
        return struct_name;
    } else if (tl_monotype_is_any(mono)) {
        return S("/*any*/void");
    } else if (tl_monotype_is_tv(mono)) {
        return S("/*tv*/void");
    } else if (tl_monotype_is_ptr(mono)) {
        return render_ptr_to_c(self, mono);
    }

    else {
        // do not fatal here: instead return a valid type, but caller will probably not use it.
        str tmp = tl_monotype_to_string(self->transient, mono);
        if (self->verbose) fprintf(stderr, "can't render a type variable: %s\n", str_cstr(&tmp));
        return S("/*untyped*/void*");
    }
}
static str type_to_c_mono(transpile *self, tl_monotype *type) {
    tl_polytype wrap = tl_polytype_wrap((tl_monotype *)type);
    return type_to_c(self, &wrap);
}

// Emit CArray(T, N) as "T[N]" — the non-decayed C spelling.
// Returns empty string if the type is not a CArray.
static str carray_type_to_c(transpile *self, tl_monotype *type) {
    if (!tl_monotype_is_carray(type)) return str_empty();
    str typec = type_to_c_mono(self, tl_monotype_carray_element(type));
    if (tl_monotype_carray_count_is_macro(type)) {
        str macro  = tl_monotype_carray_count_macro_name(type);
        str c_name = remove_c_prefix(self->transient, macro);
        return str_fmt(self->transient, "%s[%.*s]", str_cstr(&typec), str_ilen(c_name), str_buf(&c_name));
    }
    i32 count = tl_monotype_carray_count(type);
    return str_fmt(self->transient, "%s[%i]", str_cstr(&typec), count);
}

// Like type_to_c_mono but preserves CArray as T[N] instead of decaying to T*.
static str type_to_c_for_sizeof(transpile *self, tl_monotype *type) {
    str arr = carray_type_to_c(self, type);
    return str_is_empty(arr) ? type_to_c_mono(self, type) : arr;
}

static str arrow_rhs_to_c(transpile *self, tl_polytype *type) {
    if (tl_polytype_is_scheme(type)) {
        return S("void");
    }

    if (!tl_monotype_is_arrow(type->type)) fatal("expected arrow");
    tl_monotype *right = tl_monotype_sized_last(type->type->list.xs);
    return type_to_c_mono(self, right);
}

static void build_arrow_to_c(transpile *self, str_build *b, tl_monotype *type, str name) {
    (void)self;
    if (!tl_monotype_is_arrow(type)) fatal("logic error");

    str_build_cat(b, S("tl_closure"));
    if (!str_is_empty(name)) {
        str_build_cat(b, S(" "));
        str_build_cat(b, name);
    }
}

static str arrow_to_c_params(transpile *self, tl_polytype *type, str_sized param_names) {
    // param_names may be empty, e.g. when printing a prototype with no param names.
    if (tl_polytype_is_scheme(type)) fatal("type scheme");
    if (!tl_monotype_is_arrow(type->type)) fatal("expected arrow");

    str_build    b     = str_build_init(self->transient, 64);

    tl_monotype *arrow = type->type;
    if (tl_arrow != arrow->tag) fatal("logic error");
    assert(arrow->list.xs.size == 2);
    assert(tl_tuple == arrow->list.xs.v[0]->tag);
    tl_monotype_sized params = arrow->list.xs.v[0]->list.xs;
    assert(!param_names.size || param_names.size == params.size);

    // All Tess functions get void* tl_ctx as first parameter (unified closure convention)
    cat(self, S("void* tl_ctx_raw"));
    if (params.size) cat_commasp(self);

    // if no regular params, we already emitted the ctx param
    if (!params.size) {
        return str_build_finish(&b);
    }

    for (u32 i = 0, n = params.size; i < n; ++i) {
        tl_monotype *arg = params.v[i];
        str          pname =
          (i < param_names.size) ? escape_c_keyword(self->transient, param_names.v[i]) : str_empty();
        if (tl_monotype_is_arrow(arg)) {
            build_arrow_to_c(self, &b, arg, pname);
        } else {
            str tc = tl_monotype_is_void(arg) ? S("char") : type_to_c_mono(self, arg);
            str_build_cat(&b, tc);
            if (!str_is_empty(pname)) {
                str_build_cat(&b, S(" "));
                str_build_cat(&b, pname);
            }
        }

        if (i + 1 < n) str_build_cat(&b, S(", "));
    }

    return str_build_finish(&b);
}

tl_monotype *env_lookup(transpile *self, str name) {
    // may return null if type is missing or is a type scheme
    tl_polytype *type = tl_type_env_lookup(self->env, name);
    if (!type) return null;
    if (tl_polytype_is_scheme(type)) return null;
    return type->type;
}

//

static void update_type(transpile *self, tl_monotype **type) {
    // replace type with its specialized version. tl_infer had no chance to do this because it doesn't
    // know about how to handle _tl_sizeof_'s arguments.
    tl_monotype *replace = tl_infer_update_specialized_type(self->infer, *type);
    if (replace) *type = replace;
}

// Resolve the type from a nullary call with a single type argument, e.g. sizeof[T]().
// Returns the resolved monotype, or null if the node does not have that form.
static tl_monotype *resolve_nullary_type_argument(transpile *self, ast_node const *node) {
    if (node->named_application.n_arguments != 0 || node->named_application.n_type_arguments != 1)
        return null;

    ast_node const *type_arg = node->named_application.type_arguments[0];
    tl_monotype    *type     = null;

    if (type_arg->type) {
        type = type_arg->type->type;
    } else if (ast_node_is_nfa(type_arg)) {
        type = tl_type_registry_parse_type(self->registry, type_arg);
    } else if (ast_node_is_symbol(type_arg)) {
        tl_polytype *poly = tl_type_env_lookup(self->env, ast_node_str(type_arg));
        if (poly) {
            type = poly->type;
        }
    }

    if (type) update_type(self, &type);
    return type;
}

static str tl_sizeof(transpile *self, ast_node const *node, eval_ctx *ctx, void *extra) {
    (void)extra;

    assert(ast_node_is_nfa(node));

    // nullary with type argument: sizeof[T]()
    tl_monotype *type = resolve_nullary_type_argument(self, node);
    if (type) {
        // sizeof(void) is a GCC extension; MSVC rejects _Alignof(void) and warns on sizeof(void).
        // Unresolved type variables can appear in generic templates (library mode) where
        // the function is emitted without specialization. In specialized functions, a TV
        // here indicates a type parameter resolution bug.
        if (tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type))
            return S("(size_t)0");
        str ctype = type_to_c_for_sizeof(self, type);
        return str_cat_3(self->transient, S("sizeof("), ctype, S(")"));
    } else if (node->named_application.n_arguments == 0) {
        exit_error(node->file, node->line, "sizeof: could not resolve type argument");
    }

    // single argument may be an expression or a type constructor
    if (1 != node->named_application.n_arguments)
        exit_error(node->file, node->line, "sizeof expects exactly one argument");
    ast_node const *arg = node->named_application.arguments[0];

    if (ast_node_is_nfa(arg)) {
        // type constructor
        tl_monotype *type = tl_type_registry_parse_type(self->registry, arg);
        if (!type) exit_error(arg->file, arg->line, "sizeof: unknown type");
        update_type(self, &type);

        if (tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type))
            return S("(size_t)0");
        str ctype = type_to_c_for_sizeof(self, type);
        return str_cat_3(self->transient, S("sizeof("), ctype, S(")"));
    } else {
        // expression — use type info directly to avoid CArray→pointer decay.
        tl_polytype *poly = arg->type;
        if (ast_node_is_symbol(arg)) {
            tl_polytype *env_poly = tl_type_env_lookup(self->env, ast_node_str(arg));
            if (env_poly) poly = env_poly;
        }
        if (poly && poly->type && tl_monotype_is_carray(poly->type)) {
            tl_monotype *mono = poly->type;
            update_type(self, &mono);
            str carray_c = carray_type_to_c(self, mono);
            if (!str_is_empty(carray_c)) return str_cat_3(self->transient, S("sizeof("), carray_c, S(")"));
        }
        int save         = ctx->want_lvalue;
        ctx->want_lvalue = 1;
        str expr         = generate_expr(self, null, arg, ctx);
        ctx->want_lvalue = save;
        return str_cat_3(self->transient, S("sizeof("), expr, S(")"));
    }
}

static str tl_alignof(transpile *self, ast_node const *node, eval_ctx *ctx, void *extra) {
    (void)ctx;
    (void)extra;

    assert(ast_node_is_nfa(node));

    // nullary with type argument: alignof[T]()
    tl_monotype *type = resolve_nullary_type_argument(self, node);
    if (type) {
        // _Alignof(void) is a GCC extension; MSVC rejects it.
        if (tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type))
            return S("(size_t)1");
        str ctype = type_to_c_for_sizeof(self, type);
        return str_cat_3(self->transient, S("_Alignof("), ctype, S(")"));
    } else if (node->named_application.n_arguments == 0) {
        exit_error(node->file, node->line, "alignof: could not resolve type argument");
    }

    // single argument may be an expression or a type constructor
    if (1 != node->named_application.n_arguments)
        exit_error(node->file, node->line, "alignof expects exactly one argument");
    ast_node const *arg  = node->named_application.arguments[0];

    tl_polytype    *poly = arg->type;
    if (ast_node_is_symbol(arg) && !tl_polytype_is_concrete(poly))
        poly = tl_type_env_lookup(self->env, ast_node_str(arg));

    if (ast_node_is_nfa(arg)) {
        // type constructor
        tl_monotype *type = tl_type_registry_parse_type(self->registry, arg);
        if (!type) exit_error(arg->file, arg->line, "alignof: unknown type");
        update_type(self, &type);

        if (tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type))
            return S("(size_t)1");
        str ctype = type_to_c_for_sizeof(self, type);
        return str_cat_3(self->transient, S("_Alignof("), ctype, S(")"));
    } else {
        // expression - not support because MSVC makes us sad
        exit_error(node->file, node->line, "alignof(expression) is not supported; use alignof(Type)");
    }
}

static str tl_fatal(transpile *self, ast_node const *node, eval_ctx *ctx, void *extra) {
    (void)extra;
    (void)ctx;
    assert(ast_node_is_nfa(node));

    if (1 != node->named_application.n_arguments)
        exit_error(node->file, node->line, "fatal() expects exactly one argument");
    ast_node const *arg = node->named_application.arguments[0];
    if (ast_string != arg->tag) {
        exit_error(arg->file, arg->line, "fatal() argument must be a string literal");
    }

    ctx->is_effective_void = 1;

    str msg                = ast_node_str(arg);

    return str_cat_3(self->transient, S("(fprintf(stderr, \"%s\\n\", \""), msg, S("\"), exit(1))"));
}

static str generate_funcall_intrinsic(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(ast_node_is_nfa(node));
    str name = ast_node_str(node->named_application.name);

    struct dispatch {
        char const *name;
        str (*fun)(transpile *, ast_node const *, eval_ctx *, void *extra);
        void *extra;
    };

    static const struct dispatch table[] = {
      {"_tl_alignof_", tl_alignof, null},
      {"_tl_fatal_", tl_fatal, null},
      {"_tl_sizeof_", tl_sizeof, null},
      // {"_tl_sizeoft_", tl_sizeoft, null},

      {"", null, null},
    };

    // NOTE: matches prefix of name limited to length of the defined intrinsics, because inference may
    // replace applications with phantom specialised names, which have a numeric suffix.
    struct dispatch const *p = table;
    for (; p && p->name[0]; ++p)
        if (0 == str_cmp_nc(name, p->name, strlen(p->name))) return p->fun(self, node, ctx, p->extra);

    return str_empty();
}

noreturn static void exit_error(char const *file, u32 line, char const *restrict fmt, ...) {
    char buf[256];
    if (file) {
        snprintf(buf, sizeof buf, "%s:%u: error: %s\n", file, line, fmt);
    } else {
        snprintf(buf, sizeof buf, "error: %s\n", fmt);
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);

    exit(1);
}

//
// c_export support
//

// Check if a toplevel node has [[c_export]] or [[c_export("name")]].
// Returns 1 if found. Sets *out_export_name to the export name.
// Build the default export name: Module_func for non-main modules, func for main.
static str default_export_name(allocator *alloc, ast_node *name_node) {
    str orig   = ast_node_name_original(name_node);
    str module = name_node->symbol.module;
    if (str_is_empty(module)) return orig;
    return str_cat_3(alloc, module, S("_"), orig);
}

static int get_c_export_name(allocator *alloc, ast_node *toplevel_node, str *out_export_name) {
    ast_node *name_node = toplevel_name_node(toplevel_node);
    if (!ast_node_is_symbol(name_node)) return 0;

    ast_node *attrs = name_node->symbol.attributes;
    if (!attrs || ast_attribute_set != attrs->tag) return 0;

    for (u8 i = 0; i < attrs->attribute_set.n; i++) {
        ast_node *attr = attrs->attribute_set.nodes[i];

        // [[c_export]] — bare symbol: use Module_func
        if (ast_node_is_symbol(attr) && str_eq(attr->symbol.name, S("c_export"))) {
            *out_export_name = default_export_name(alloc, name_node);
            return 1;
        }

        // [[c_export("custom_name")]] — NFA with one string argument
        if (ast_node_is_nfa(attr)) {
            ast_node *nfa_name = attr->named_application.name;
            if (ast_node_is_symbol(nfa_name) && str_eq(nfa_name->symbol.name, S("c_export"))) {
                if (attr->named_application.n_arguments == 1) {
                    ast_node *arg = attr->named_application.arguments[0];

                    // Accept NFA-wrapped strings:
                    if (ast_node_is_nfa(arg) && arg->named_application.n_arguments == 1)
                        arg = arg->named_application.arguments[0];

                    if (ast_string == arg->tag) {
                        *out_export_name = arg->symbol.name;
                        return 1;
                    }
                }
                // c_export with non-string argument: use default
                *out_export_name = default_export_name(alloc, name_node);
                return 1;
            }
        }
    }
    return 0;
}

// Check whether a monotype is suitable for C export.
static int is_c_exportable_type(tl_monotype *type) {
    if (!type) return 0;

    if (tl_monotype_is_any(type) || tl_monotype_is_tv(type)) return 0;
    if (tl_monotype_is_arrow(type)) return 0;
    if (tl_monotype_is_tuple(type)) return 0;

    if (tl_monotype_is_ptr(type)) {
        tl_monotype *target = tl_monotype_ptr_target(type);
        target              = tl_monotype_strip_const(target);
        if (tl_monotype_is_any(target)) return 1; // Ptr[any] is opaque, ok
        return is_c_exportable_type(target);
    }

    if (tl_monotype_is_const(type)) {
        return is_c_exportable_type(tl_monotype_const_target(type));
    }

    if (!tl_monotype_is_concrete_inst(type)) return 0;

    str name = type->cons_inst->def->name;

    // Builtin types with c_type_name are C-compatible (integers, floats, Void, Bool)
    if (!str_is_empty(type->cons_inst->def->c_type_name)) return 1;
    if (is_c_symbol(name)) return 1;

    // Disallowed: Str, user structs, tagged unions, enums, etc.
    return 0;
}

// Check if any toplevel has c_export attribute.
static int has_c_exports(transpile *self) {
    str dummy;
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;
        if (get_c_export_name(self->transient, node, &dummy)) return 1;
    }
    return 0;
}

// Validate that all c_export functions have C-compatible types.
static void validate_c_exports(transpile *self) {
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;

        str export_name;
        if (!get_c_export_name(self->transient, node, &export_name)) continue;

        str          name = toplevel_name(node);
        tl_polytype *poly = tl_type_env_lookup(self->env, name);
        if (!poly) continue;
        if (!should_generate(self, name, poly)) continue;

        ast_node    *name_node = toplevel_name_node(node);
        tl_monotype *arrow     = poly->type;
        if (!tl_monotype_is_arrow(arrow)) continue;

        tl_monotype_sized params = tl_monotype_arrow_get_args(arrow);
        for (u32 j = 0; j < params.size; j++) {
            if (!is_c_exportable_type(params.v[j])) {
                str tname = tl_monotype_to_string(self->transient, params.v[j]);
                exit_error(name_node->file, name_node->line,
                           "c_export function '%s' has non-C-exportable parameter type: %s",
                           str_cstr(&export_name), str_cstr(&tname));
            }
        }

        tl_monotype *ret = tl_monotype_sized_last(arrow->list.xs);
        if (!is_c_exportable_type(ret)) {
            str tname = tl_monotype_to_string(self->transient, ret);
            exit_error(name_node->file, name_node->line,
                       "c_export function '%s' has non-C-exportable return type: %s",
                       str_cstr(&export_name), str_cstr(&tname));
        }
    }
}

// Generate non-static wrapper functions for c_export functions.
static void generate_c_exports(transpile *self) {
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;
        if (ast_node_is_let(node) && !ast_node_is_specialized(node)) continue;

        str export_name;
        if (!get_c_export_name(self->transient, node, &export_name)) continue;

        str          name = toplevel_name(node);
        tl_polytype *poly = tl_type_env_lookup(self->env, name);
        if (!poly) continue;
        if (!should_generate(self, name, poly)) continue;

        tl_monotype *arrow = poly->type;
        if (!tl_monotype_is_arrow(arrow)) continue;

        tl_monotype_sized params = tl_monotype_arrow_get_args(arrow);
        tl_monotype      *ret    = tl_monotype_sized_last(arrow->list.xs);

        // Generate wrapper: TL_EXPORT ret_type export_name(params) { return mangled_name(args); }
        str ret_c       = type_to_c_mono(self, ret);
        int res_is_void = str_eq(ret_c, S("void"));

        cat(self, S("TL_EXPORT "));
        cat(self, ret_c);
        cat_sp(self);
        cat(self, export_name);
        cat_open_round(self);

        // Build parameter list with names
        if (!params.size) {
            cat(self, S("void"));
        } else {
            for (u32 j = 0; j < params.size; j++) {
                str ptype = type_to_c_mono(self, params.v[j]);
                cat(self, ptype);
                cat_sp(self);
                // Generate parameter names: tl_p0, tl_p1, ...
                char pbuf[32];
                snprintf(pbuf, sizeof pbuf, "tl_p%u", j);
                cat(self, str_init_small(pbuf));
                if (j + 1 < params.size) cat_commasp(self);
            }
        }

        cat_close_round(self);
        cat_sp(self);
        cat_open_curlyln(self);

        // Body: return mangled_name(NULL, tl_p0, tl_p1, ...);
        if (!res_is_void) cat(self, S("return "));
        cat(self, mangle_fun(self, name));
        cat_open_round(self);
        cat(self, S("NULL"));
        for (u32 j = 0; j < params.size; j++) {
            cat_commasp(self);
            char pbuf[32];
            snprintf(pbuf, sizeof pbuf, "tl_p%u", j);
            cat(self, str_init_small(pbuf));
        }
        cat_close_round(self);
        cat_semicolonln(self);

        cat_close_curly(self);
        cat_nl(self);
        cat_nl(self);
    }
}

// Generate a C header file with prototypes for all c_export functions.
int transpile_generate_header(transpile *self, str_build *out_header, str guard_name) {
    if (!has_c_exports(self)) return 0;

    str_build hdr = str_build_init(self->transient, 512);

    str_build_cat(&hdr, S("#ifndef "));
    str_build_cat(&hdr, guard_name);
    str_build_cat(&hdr, S("\n#define "));
    str_build_cat(&hdr, guard_name);
    str_build_cat(&hdr, S("\n\n"));
    str_build_cat(&hdr, S("#include <stddef.h>\n"));
    str_build_cat(&hdr, S("#include <stdint.h>\n\n"));
    str_build_cat(&hdr, S("/* Initialize the Tess runtime. Call before any exported function. */\n"));
    if (!str_is_empty(self->opts.lib_name)) {
        str_build_cat(&hdr, S("void tl_init_"));
        str_build_cat(&hdr, self->opts.lib_name);
        str_build_cat(&hdr, S("(void);\n\n"));
    } else {
        str_build_cat(&hdr, S("void tl_init(void);\n\n"));
    }

    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;
        if (ast_node_is_let(node) && !ast_node_is_specialized(node)) continue;

        str export_name;
        if (!get_c_export_name(self->transient, node, &export_name)) continue;

        str          name = toplevel_name(node);
        tl_polytype *poly = tl_type_env_lookup(self->env, name);
        if (!poly) continue;
        if (!should_generate(self, name, poly)) continue;

        tl_monotype *arrow = poly->type;
        if (!tl_monotype_is_arrow(arrow)) continue;

        tl_monotype_sized params = tl_monotype_arrow_get_args(arrow);
        tl_monotype      *ret    = tl_monotype_sized_last(arrow->list.xs);

        // return type
        str ret_c = type_to_c_mono(self, ret);
        str_build_cat(&hdr, ret_c);
        str_build_cat(&hdr, S(" "));
        str_build_cat(&hdr, export_name);
        str_build_cat(&hdr, S("("));

        if (!params.size) {
            str_build_cat(&hdr, S("void"));
        } else {
            for (u32 j = 0; j < params.size; j++) {
                str ptype = type_to_c_mono(self, params.v[j]);
                str_build_cat(&hdr, ptype);
                if (j + 1 < params.size) str_build_cat(&hdr, S(", "));
            }
        }

        str_build_cat(&hdr, S(");\n"));
    }

    str_build_cat(&hdr, S("\n#endif\n"));

    *out_header = hdr;
    return 1;
}
