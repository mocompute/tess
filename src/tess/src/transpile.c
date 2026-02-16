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
    hashmap          *structs;           // u64 set
    hashmap          *context_generated; // str set

    str_array         toplevels_sorted;

    str_build         build;

    u64               next_block;
    u64               next_res;

    int               no_line_directive;
    int               verbose;
};

typedef struct defer_scope {
    ast_node_sized      defers;   // this body's ast_body.defers
    struct defer_scope *parent;   // enclosing scope
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

    // the type of the expression just evaluated is effectively void, regardless of its declared type. For
    // example, _tl_fatal_ is -> any, but when it appears it should never be assigned to a result variable.
    int is_effective_void;

    defer_scope *defers;              // innermost defer scope (linked list head)
    defer_scope *loop_defer_boundary; // snapshot at loop entry; break/continue stop here

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
static str         generate_funcall_intrinsic(transpile *, ast_node const *, eval_ctx *);
static void        generate_prototypes(transpile *, int);
static void        generate_structs(transpile *);
static void        generate_toplevel_values(transpile *);
static void        generate_toplevels(transpile *);
static void        generate_assign_lhs(transpile *, str);
static void        generate_assign(transpile *, str, str);
static void        generate_assign_field(transpile *, str, str, str);

static void        cat(transpile *, str);
static void        cat_nl(transpile *);
static void        cat_sp(transpile *);
static void        cat_ampersand(transpile *);
static void        cat_assign(transpile *);
static void        cat_commasp(transpile *);
static void        cat_dot(transpile *);
static void        cat_open_round(transpile *);
static void        cat_close_round(transpile *);
static void        cat_open_curly(transpile *);
static void        cat_open_curlyln(transpile *);
static void        cat_close_curly(transpile *);
static void        cat_close_square(transpile *);
static void        cat_semicolon(transpile *);
static void        cat_semicolonln(transpile *);
static void        cat_star(transpile *);
static void        cat_return(transpile *, str);
static void        catln(transpile *, str);
static void        cat_comment(transpile *, str);
static void        cat_commentln(transpile *, str);
// static void        cat_double_slash(transpile *);
static void cat_close_curlyln(transpile *);
// static void        cat_i64(transpile *, i64);
// static void        cat_f64(transpile *, f64);

tl_monotype *env_lookup(transpile *, str); // may be null
static str   mangle_fun(transpile *, str); // allocates transient
static int   should_generate(transpile *, str, tl_polytype *);
static str   type_to_c(transpile *, tl_polytype *);
static str   type_to_c_mono(transpile *, tl_monotype *);
static str   arrow_rhs_to_c(transpile *, tl_polytype *);
static str   arrow_to_c_params(transpile *, tl_polytype *, str_sized); // allocates transient
static void  build_arrow_to_c(transpile *, str_build *, tl_monotype *, str);
static str   ptr_to_arrow_to_c(transpile *, tl_monotype *);
static str   ptr_to_arrow_decl(transpile *, tl_monotype *, str);
static void  generate_function_signature(transpile *, str, tl_polytype *, str_sized);
static void  exit_error(char const *file, u32 line, char const *restrict fmt, ...);
static void  update_type(transpile *, tl_monotype **);

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
    tl_monotype *arrow    = type->type;
    tl_monotype *ret_type = arrow->list.xs.v[1];

    if (tl_monotype_is_arrow(ret_type)) {
        // Function returning function pointer: ret (*name(params))(fp_params)
        str_build sig = str_build_init(self->transient, 64);
        str_build_cat(&sig, mangle_fun(self, name));
        str_build_cat(&sig, S("("));
        str_build_cat(&sig, arrow_to_c_params(self, type, param_names));
        str_build_cat(&sig, S(")"));
        str       func_sig = str_build_finish(&sig);

        str_build b        = str_build_init(self->transient, 80);
        build_arrow_to_c(self, &b, ret_type, func_sig);
        cat(self, str_build_finish(&b));
    } else {
        // Normal return type: ret name(params)
        str ret = arrow_rhs_to_c(self, type);
        cat(self, ret);
        cat_sp(self);
        cat(self, mangle_fun(self, name));
        cat_open_round(self);
        cat(self, arrow_to_c_params(self, type, param_names));
        cat_close_round(self);
    }
}

static void generate_prototypes(transpile *self, int decl_static) {
    forall(i, self->toplevels_sorted) {
        ast_node *node = str_map_get_ptr(self->toplevels, self->toplevels_sorted.v[i]);
        if (ast_node_is_utd(node)) continue;

        str          name = toplevel_name(node);
        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("missing type");

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

static str context_name(transpile *self, str_sized fvs) {
    // generate struct name using hash of fvs
    u64  hash = str_array_hash64(0, fvs);
    char buf[64];
    snprintf(buf, sizeof buf, "tl_ctx_%" PRIu64, hash);
    return str_init(self->transient, buf);
}

static void generate_context_struct(transpile *self, str_sized fvs) {
    str name = context_name(self, fvs);
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
        generate_decl_pointer(self, field, field_type->type);
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

static str generate_expr_symbol(transpile *self, tl_monotype *type, str symbol_name, str original_name,
                                eval_ctx *ctx) {
    str name = symbol_name;

    if (tl_monotype_is_arrow(type)) name = mangle_fun(self, name);

    // c_ prefixed symbols are always emitted literally
    if (is_c_symbol(name)) {
        return remove_c_prefix(self->transient, name);
    }

    // escape identifiers that clash with C reserved keywords
    name = escape_c_keyword(self->transient, name);

    if (ctx && str_array_contains_one(ctx->free_variables, symbol_name)) // unmangled name
    {
        // generate reference through context
        return str_cat_4(self->transient, S("("), S("*tl_ctx->"), name, S(")"));
    } else {
        if (useful_name(original_name, name)) {
            // TODO: put this behind an option
            return str_cat_4(self->transient, S("/*"), original_name, S("*/ "), name);
        } else {
            return name;
        }
    }
}

static str generate_context(transpile *self, str_sized fvs, eval_ctx *ctx) {
    if (!fvs.size) return str_empty();
    str name = context_name(self, fvs);

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

        str          name = fvs.v[i];
        tl_polytype *type = tl_type_env_lookup(self->env, name);
        if (!type) fatal("runtime error");
        name = generate_expr_symbol(self, type->type, name, str_empty(), ctx);
        cat(self, name);

        cat_close_round(self);

        if (i + 1 < fvs.size) cat_commasp(self);
    }

    cat_close_curly(self);
    cat_semicolonln(self);

    return ctx_var;
}

static void generate_toplevel_contexts(transpile *self) {

    hashmap_iterator iter = {0};
    while (map_iter(self->env->map, &iter)) {
        tl_polytype *type = *(tl_polytype **)iter.data;

        if (type->type->tag == tl_arrow && type->type->list.fvs.size) {
            generate_context_struct(self, type->type->list.fvs);
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
        tl_polytype *type = node->let_in.value->type;
        if (!tl_polytype_is_concrete(type)) continue;

        cat(self, S("TL_THREAD_LOCAL "));
        generate_decl(self, name, type->type);
    }

    cat_nl(self);

    // tl_init function
    if (self->opts.is_library) cat(self, S("void tl_init(void) {\n"));
    else cat(self, S("static void tl_init(void) {\n"));

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
                cat_open_round(self);
                cat_close_round(self);
                cat_semicolonln(self);
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
        eval_ctx ctx      = {.free_variables = poly->type->list.fvs};
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
    if (!main) fatal("no main function");
    if (ast_let != main->tag) fatal("logic error");

    tl_monotype      *type = env_lookup(self, S("main"));
    tl_monotype_sized args = {0};
    if (type && tl_monotype_is_arrow(type)) {
        args = tl_monotype_arrow_get_args(type);
    }

    if (!type || !args.size) {
        cat(self, S("int main(void) { tl_init(); return tl_fun_main(); }\n"));
    } else if (1 == args.size) {
        cat(self, S("int main(int argc) { tl_init(); return tl_fun_main(argc); }\n"));
    } else if (2 == args.size) {
        cat(self, S("int main(int argc, char* argv[]) { tl_init(); return tl_fun_main(argc, argv); }\n"));
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

static void generate_assign_op(transpile *self, str lhs, str rhs, str op) {
    cat(self, lhs);
    if (!str_is_empty(op)) cat(self, op);
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

static void generate_funcall_head_ext(transpile *self, str name, str ctx_var, u32 n_args, int do_mangle) {

    if (do_mangle) cat(self, mangle_fun(self, name));
    else cat(self, name);
    cat_open_round(self);

    if (!str_is_empty(ctx_var)) {
        cat_ampersand(self);
        cat(self, ctx_var);
        if (n_args) cat_commasp(self);
    }
}

static void generate_funcall_head(transpile *self, str name, str ctx_var, u32 n_args) {
    generate_funcall_head_ext(self, name, ctx_var, n_args, 1);
}

static void generate_funcall_head_no_mangle(transpile *self, str name, str ctx_var, u32 n_args) {
    generate_funcall_head_ext(self, name, ctx_var, n_args, 0);
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
        str res = generate_expr(self, arr.v[i], args.v[i], ctx);
        array_push(args_res, res);
    }
    return args_res;
}

static int is_nil_result(tl_monotype *type) {
    return tl_monotype_is_void(type) || tl_monotype_is_tv(type) || tl_monotype_is_any(type) ||
           tl_monotype_is_weak(type);
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

        str arg_value = generate_expr(self, type->cons_inst->args.v[found], arg->assignment.value, ctx);

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
    assert(tl_monotype_is_inst(type));

    str                      res = next_res(self);
    tl_type_constructor_def *def = type->cons_inst->def;

    assert(def->field_names.size == node->named_application.n_arguments);
    assert(def->field_names.size == type->cons_inst->args.size);

    generate_decl(self, res, type);

    forall(i, def->field_names) {
        str arg_value =
          generate_expr(self, type->cons_inst->args.v[i], node->named_application.arguments[i], ctx);

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

static str generate_funcall_std(transpile *self, ast_node const *node, eval_ctx *ctx) {

    // a funcall to a std function is fundamentally different: we don't have type information on the
    // function or the arguments.

    // generate untyped arguments
    str            name     = ast_node_str(node->named_application.name);
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = {.alloc = self->transient};
    array_reserve(args_res, args.size);

    forall(i, args) {
        str res = generate_expr(self, null, args.v[i], ctx);
        array_push(args_res, res);
    }

    // Note: all std_ functions have nil result

    // function call
    generate_funcall_head(self, name, str_empty(), args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return str_empty();
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

    forall(i, args) {
        str res = generate_expr(self, null, args.v[i], ctx);
        array_push(args_res, res);
    }

    // declare variable to hold funcall result if it's not nil
    tl_monotype *type = env_lookup(self, ast_node_str(node->named_application.name));

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

    // function call
    if (!str_is_empty(res)) generate_assign_lhs(self, res);
    generate_funcall_head_no_mangle(self, name, str_empty(), args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static str generate_funcall_with_args(transpile *self, ast_node const *node, eval_ctx *ctx,
                                      str_array args_res) {

    str          name = ast_node_str(node->named_application.name);
    tl_monotype *type = env_lookup(self, name);

    // declare variable to hold funcall result if it's not nil
    str res = generate_funcall_result(self, type);

    assert(tl_monotype_is_list(type));
    str ctx_var = str_empty();
    if (type->list.fvs.size) {
        ctx_var = generate_context(self, type->list.fvs, ctx);
    }

    // function call
    if (!str_is_empty(res)) generate_assign_lhs(self, res);
    generate_funcall_head(self, name, ctx_var, args_res.size);

    // args list
    str_build b = str_build_init(self->transient, 128);
    str_build_join_array(&b, S(", "), args_res);
    cat(self, str_build_finish(&b));
    cat_close_round(self);
    cat_semicolonln(self);

    return res;
}

static str generate_funcall(transpile *self, ast_node const *node, eval_ctx *ctx) {
    // Note: the main logic of this function is also duplicated in generate_binary_op.

    // A funcall can be a standard tl funcall, a c_ funcall, a type constructor, or a c_ type
    // constructor.

    assert(ast_node_is_nfa(node));
    str name = ast_node_str(node->named_application.name);
    if (is_intrinsic(name)) return generate_funcall_intrinsic(self, node, ctx);
    if (0 == str_cmp_nc(name, "std_", 4)) return generate_funcall_std(self, node, ctx);

    // c_ prefix: may be a c_ funcall or a c_ type constructor. If there is no type
    tl_monotype *type = env_lookup(self, name);

    // type constructor?
    if (type && tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

    // check c_ after type constructor
    if (is_c_symbol(name)) return generate_funcall_c(self, node, ctx);

    if (!type) {
        // FIXME: file/line here is not accurate
        exit_error(node->file, node->line, "unknown function or function type: %s (%s:%i)", str_cstr(&name),
                   __FILE__, __LINE__);
    }

    // type constructor?
    if (tl_monotype_is_inst(type)) return generate_type_constructor(self, node, ctx);

    // generate arguments: an array of variables will hold their values
    ast_node_sized args     = ast_node_sized_from_ast_array((ast_node *)node);
    str_array      args_res = generate_args(self, args, type, ctx);

    return generate_funcall_with_args(self, node, ctx, args_res);
}

static str generate_let_in_lambda(transpile *self, tl_monotype *result_type, ast_node const *node,
                                  eval_ctx *ctx) {

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

static str generate_let_in(transpile *self, tl_monotype *result_type, ast_node const *node, eval_ctx *ctx) {
    if (ast_node_is_let_in_lambda(node)) return generate_let_in_lambda(self, result_type, node, ctx);
    assert(ast_node_is_let_in(node));

    str          name = ast_node_str(node->let_in.name);
    tl_monotype *type = env_lookup(self, name); // may be null
    name              = escape_c_keyword(self->transient, name);

    if (type) {

        if (!should_assign_value(node->let_in.value)) {
            // binding a symbol to void means to declare it without initialising it
            generate_decl(self, name, type);
        } else {
            str value = generate_expr(self, type, node->let_in.value, ctx);

            if (tl_monotype_is_tv(type) || str_is_empty(value)) {
                // The assignment target has an indeterminate type, most likely because `value` is an
                // unknown symbol.
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
            } else if (tl_monotype_is_concrete(type)) {
                if (should_assign_result(ctx, type)) {
                    generate_decl(self, name, type);

                    // Note: special case: if we are assigning to a pointer type, cast the rhs to that type.
                    // This allows C pointer casts without a warning.
                    if (should_assign_value(node->let_in.value)) {
                        if (tl_monotype_is_ptr(type)) {
                            cat(self, name);
                            cat_assign(self);

                            cat_open_round(self);
                            cat(self, type_to_c_mono(self, type));
                            cat_close_round(self);

                            cat(self, value);
                            cat_semicolonln(self);
                        } else {
                            generate_assign(self, name, value);
                        }
                    }
                }
            } else {
                // Note: do not emit values that are not concrete. These can come out of type inference if
                // the variable is never referenced, so it is safe to avoid emitting them. Conversely, we
                // can't correctly emit them because the type information is incomplete. However, there are
                // exceptions: return value type information is not always available for c_ functions, so we
                // emit all non-arrow values and c_* arrow values.
                if (is_c_symbol(value) || !tl_monotype_is_arrow(type)) {

                    generate_decl(self, name, type);
                    if (should_assign_value(node->let_in.value)) generate_assign(self, name, value);
                }
            }
        }
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
    if (params.size != args.size) fatal("runtime error");

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
        scope = (defer_scope){.defers = node->body.defers, .parent = ctx->defers};
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

// Generate case expression for tagged union pattern matching
// case s: Shape { c: Circle { ... } sq: Square { ... } }
// Generates:
//   if (s.tag == Foo__ShapeTag__Circle) { Foo_Circle c = s.u.Circle; ... }
//   else if (s.tag == Foo__ShapeTag__Square) { ... }
static str generate_tagged_union_case(transpile *self, ast_node const *node, eval_ctx *ctx) {
    assert(node->case_.is_union);
    int is_pointer = node->case_.is_union == AST_TAGGED_UNION_MUTABLE;

    // Generate the expression being matched
    str expr_str = generate_expr(self, null, node->case_.expression, ctx);

    // Get the wrapper type (Shape)
    tl_monotype *wrapper_type = node->case_.expression->type->type;
    if (!tl_monotype_is_inst(wrapper_type)) fatal("expected tagged union wrapper type");

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
                // TODO: make this an error
                fatal("else must be last");
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
            break;
        }

        ast_node *cond = node->case_.conditions.v[i];
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation) {
            fatal("tagged union case condition must be 'binding: VariantType'");
        }

        // Get variant type from the condition's annotation
        ast_node    *variant_type_node = cond->symbol.annotation;
        tl_monotype *variant_type      = cond->symbol.annotation_type->type;

        if (!tl_monotype_is_inst(variant_type)) fatal("expected variant type");

        // Get the variant name (unmangled) from the annotation
        str variant_name = ast_node_name_original(variant_type_node);

        // Get the tag type from the wrapper's first field (tag)
        // The wrapper has fields in order: tag, u
        tl_monotype *tag_type    = null;
        str_sized    field_names = wrapper_type->cons_inst->def->field_names;
        forall(f, field_names) {
            if (str_eq(field_names.v[f], S("tag"))) {
                tag_type = wrapper_type->cons_inst->args.v[f];
                break;
            }
        }
        if (!tag_type) fatal("wrapper type missing 'tag' field");
        str tag_enum_name = tag_type->cons_inst->def->name;

        // Build tag value name: TagEnumName_VariantName
        // e.g., Foo__ShapeTag_ + "_" + Circle -> Foo__ShapeTag__Circle
        str tag_value_name = str_cat_3(self->transient, tag_enum_name, S("__"), variant_name);

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
            str mangled = str_cat_3(self->transient, name, S("__"), ast_node_str(node->binary_op.right));
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
    int carray_field = is_struct_access_operator(str_cstr(&op)) && node->binary_op.right->type &&
                       tl_monotype_is_inst_of(node->binary_op.right->type->type, S("CArray"));

    int save_lvalue = ctx->want_lvalue;
    if (carray_field) ctx->want_lvalue = 1;
    str left         = generate_expr(self, null, node->binary_op.left, ctx);
    ctx->want_lvalue = save_lvalue;
    str right;

    // Note: special case if right hand is a funcall of a struct member
    if (ast_node_is_nfa(node->binary_op.right) && is_struct_access_operator(str_cstr(&op))) {
        // To handle obj.fun() and obj->fun(), we first load the function pointer from the field `fun`,
        // then invoke the funcall logic. The named_application.name node holds the function type.

        str          fun = generate_expr(self, null, node->binary_op.right->named_application.name, ctx);
        tl_monotype *fun_type = node->binary_op.right->named_application.name->type->type;

        str          fun_res  = next_res(self);
        if (!is_nil_result(fun_type)) {
            generate_decl(self, fun_res, fun_type);
            generate_assign_lhs(self, fun_res);
        }
        cat(self, left);
        cat(self, op);
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
            str ctx_var = str_empty();
            if (type->list.fvs.size) {
                ctx_var = generate_context(self, type->list.fvs, ctx);
            }

            // function call
            if (!str_is_empty(res) && !is_nil_result(type)) generate_assign_lhs(self, res);
            generate_funcall_head(self, name, ctx_var, args_res.size);

            // args list
            str_build b = str_build_init(self->transient, 128);
            str_build_join_array(&b, S(", "), args_res);
            cat(self, str_build_finish(&b));
            cat_close_round(self);
            cat_semicolonln(self);

            return res;
        }

    } else {
        right = generate_expr(self, null, node->binary_op.right, ctx);
    }

    if (!ctx->want_lvalue) {
        str res = next_res(self);
        if (!is_nil_result(type)) {
            generate_decl(self, res, type);
            generate_assign_lhs(self, res);
        }

        int is_index   = is_index_operator(str_cstr(&op));
        int is_ptr_cmp = !is_index && is_relational_operator(str_cstr(&op)) && node->binary_op.left->type &&
                         node->binary_op.right->type &&
                         tl_monotype_is_ptr(node->binary_op.left->type->type) &&
                         tl_monotype_is_ptr(node->binary_op.right->type->type);

        if (is_ptr_cmp) {
            cat(self, S("(void*)"));
        }
        cat(self, left);

        if (is_index) {
            cat(self, S("["));
            cat(self, right);
            cat_close_square(self);
        } else {
            cat(self, op);
            if (is_ptr_cmp) {
                cat(self, S("(void*)"));
            }
            cat(self, right);
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
    if (is_break)
        emit_defers_until(self, ctx, ctx->loop_defer_boundary);
    else
        emit_defers_until(self, ctx, null);

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

    str save_update_label = ctx->update_label;
    ctx->update_label     = next_label(self);

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
    if (!self->no_line_directive && ctx && node->file && node->file[0]) {
        str line = str_fmt(self->transient, "#line %u \"%s\"\n", node->line, node->file);
        if (!str_eq(line, ctx->last_line_directive)) {
            ctx->last_line_directive = line;
            cat(self, line);
        }
    }

    switch (node->tag) {
    case ast_named_function_application:  return generate_funcall(self, node, ctx);
    case ast_lambda_function_application: return generate_inline_lambda(self, type, node, ctx);
    case ast_let_in:                      return generate_let_in(self, type, node, ctx);
    case ast_i64:                         return generate_str(self, str_init_i64(self->transient, node->i64.val), type);
    case ast_u64:                         return generate_str(self, str_init_u64(self->transient, node->u64.val), type);
    case ast_f64:                         return generate_str(self, str_init_f64(self->transient, node->f64.val), type);
    case ast_bool:                        return generate_str(self, node->bool_.val ? S("1 /*true*/") : S("0 /*false*/"), type);
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
        return generate_expr_symbol(self, type, name, ast_node_name_original(node), ctx);
    }

    case ast_if_then_else:    return generate_if_then_else(self, node, ctx);

    case ast_return:          return generate_return(self, type, node, ctx);
    case ast_while:           return generate_while(self, type, node, ctx);
    case ast_continue:        return generate_continue(self, type, node, ctx);

    case ast_nil:             return S("NULL");
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
    if (tl_monotype_is_const(type)) type = tl_monotype_const_target(type);
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
        if (tl_monotype_is_inst_of(type, S("CArray")) && type->cons_inst->args.size == 2) {
            str typec = type_to_c_mono(self, type->cons_inst->args.v[0]);
            i32 count = tl_monotype_integer(type->cons_inst->args.v[1]);
            str line  = str_fmt(self->transient, "%s %s[%i];\n", str_cstr(&typec), str_cstr(&name), count);
            cat(self, line);
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
            typec = str_init(self->transient, "/*nil*/ void*");
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

    else {
        fatal("unexpected type");
    }
}

static void generate_decl_pointer(transpile *self, str name, tl_monotype *type) {
    name = escape_c_keyword(self->transient, name);
    if (tl_arrow == type->tag) {
        // arrow

        str_build b = str_build_init(self->transient, 80);
        build_arrow_to_c(self, &b, type, name);
        cat(self, str_build_finish(&b));
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

    str_build_cat(&self->build, str_init_static(embed_std_c));
    cat_nl(self);
    cat_nl(self);

    generate_hash_includes(self);

    generate_user_types(self);
    generate_structs(self);
    generate_toplevel_contexts(self);

    generate_ifc_blocks(self);
    generate_prototypes(self, !self->opts.is_library);
    cat_nl(self);

    generate_toplevel_values(self);
    cat_nl(self);

    generate_toplevels(self);
    cat_nl(self);

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
    self->synthesized_nodes = opts->infer_result.synthesized_nodes;
    self->hash_includes     = opts->infer_result.hash_includes;

    self->structs           = hset_create(self->arena, 64);
    self->context_generated = hset_create(self->arena, 64);

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

void transpile_get_arena_stats(transpile *self, arena_stats *out) {
    arena_get_stats(self->arena, out);
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
    if (0 == str_cmp_nc(s, "std_", 4)) return s; // FIXME: do we still use std_ ?

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
    if (str_eq(name, S("main"))) return 1;

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

static str type_to_c(transpile *self, tl_polytype *type) {
    if (type->quantifiers.size) fatal("type scheme");
    tl_monotype *mono = type->type;
    if (tl_monotype_is_concrete_no_arrow(mono)) {
        str cons_name = mono->cons_inst->def->name;
        if (str_eq(S("Int"), cons_name)) {
            return S("long long");
        } else if (str_eq(S("CChar"), cons_name)) {
            return S("char");
        } else if (str_eq(S("CUnsignedChar"), cons_name)) {
            return S("unsigned char");
        } else if (str_eq(S("CSignedChar"), cons_name)) {
            return S("signed char");
        } else if (str_eq(S("CShort"), cons_name)) {
            return S("short");
        } else if (str_eq(S("CUnsignedShort"), cons_name)) {
            return S("unsigned short");
        } else if (str_eq(S("CInt"), cons_name)) {
            return S("int");
        } else if (str_eq(S("CUnsignedInt"), cons_name)) {
            return S("unsigned int");
        } else if (str_eq(S("CLong"), cons_name)) {
            return S("long");
        } else if (str_eq(S("CUnsignedLong"), cons_name)) {
            return S("unsigned long");
        } else if (str_eq(S("CLongLong"), cons_name)) {
            return S("long long");
        } else if (str_eq(S("CUnsignedLongLong"), cons_name)) {
            return S("unsigned long long");
        } else if (str_eq(S("CSize"), cons_name)) {
            return S("size_t");
        } else if (str_eq(S("CPtrDiff"), cons_name)) {
            return S("ptrdiff_t");
        }

        else if (str_eq(S("CInt8"), cons_name)) {
            return S("int8_t");
        } else if (str_eq(S("CUInt8"), cons_name)) {
            return S("uint8_t");
        } else if (str_eq(S("CInt16"), cons_name)) {
            return S("int16_t");
        } else if (str_eq(S("CUInt16"), cons_name)) {
            return S("uint16_t");
        } else if (str_eq(S("CInt32"), cons_name)) {
            return S("int32_t");
        } else if (str_eq(S("CUInt32"), cons_name)) {
            return S("uint32_t");
        } else if (str_eq(S("CInt64"), cons_name)) {
            return S("int64_t");
        } else if (str_eq(S("CUInt64"), cons_name)) {
            return S("uint64_t");
        }

        else if (str_eq(S("CFloat"), cons_name)) {
            return S("float");
        } else if (str_eq(S("CDouble"), cons_name)) {
            return S("double");
        } else if (str_eq(S("CLongDouble"), cons_name)) {
            return S("long double");
        }

        else if (str_eq(S("Float"), cons_name)) {
            return S("double");
        } else if (str_eq(S("Bool"), cons_name)) {
            return S("/*bool*/int");
        } else if (str_eq(S("Void"), cons_name)) {
            return S("void");
        } else if (tl_monotype_is_ptr(mono)) {
            // Special case for Ptr(..(Arrow)..) -> int (**...*)(int) format
            str ptr_arrow = ptr_to_arrow_to_c(self, mono);
            if (!str_is_empty(ptr_arrow)) return ptr_arrow;

            // Ptr(Const(T)) -> const T*
            tl_monotype *arg = tl_monotype_ptr_target(mono);
            if (tl_monotype_is_const(arg)) {
                tl_monotype *inner = tl_monotype_const_target(arg);
                tl_polytype  wrap  = tl_polytype_wrap(inner);
                str          typec = type_to_c(self, &wrap);
                return str_cat_3(self->transient, S("const "), typec, S("*"));
            }

            // Normal Ptr handling for non-arrow targets
            tl_polytype wrap  = tl_polytype_wrap(arg);
            str         typec = type_to_c(self, &wrap);
            return str_cat(self->transient, typec, S("*"));
        }

        else if (tl_monotype_is_const(mono)) {
            // Standalone Const(T) -> T (const only meaningful inside Ptr)
            tl_monotype *inner = tl_monotype_const_target(mono);
            tl_polytype  wrap  = tl_polytype_wrap(inner);
            return type_to_c(self, &wrap);
        }

        else if (str_eq(S("CArray"), cons_name) && mono->cons_inst->args.size == 2) {
            // CArray(T, N) renders as T* (decayed pointer)
            tl_monotype *element = mono->cons_inst->args.v[0];
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
        // Special case for Ptr(..(Arrow)..) -> int (**...*)(int) format
        str ptr_arrow = ptr_to_arrow_to_c(self, mono);
        if (!str_is_empty(ptr_arrow)) return ptr_arrow;

        // Ptr(Const(T)) -> const T*
        tl_monotype *arg = tl_monotype_ptr_target(mono);
        if (tl_monotype_is_const(arg)) {
            tl_monotype *inner = tl_monotype_const_target(arg);
            tl_polytype  wrap  = tl_polytype_wrap(inner);
            str          typec = type_to_c(self, &wrap);
            return str_cat_3(self->transient, S("const "), typec, S("*"));
        }

        // Normal Ptr handling for non-arrow targets
        tl_polytype wrap  = tl_polytype_wrap(arg);
        str         typec = type_to_c(self, &wrap);
        return str_cat(self->transient, typec, S("*"));
    }

    else {
        // do not fatal here: instead return a valid type, but caller will probably not use it.
        str tmp = tl_monotype_to_string(self->transient, mono);
        if (self->verbose) fprintf(stderr, "can't render a type variable: %s\n", str_cstr(&tmp));
        return S("/*untyped*/void*");
    }

    // else
    //     fatal("can't render a type variable");
}
static str type_to_c_mono(transpile *self, tl_monotype *type) {
    tl_polytype wrap = tl_polytype_wrap((tl_monotype *)type);
    return type_to_c(self, &wrap);
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
    if (!tl_monotype_is_arrow(type)) fatal("logic error");

    if (!tl_monotype_is_arrow(type)) fatal("expected arrow");
    assert(type->list.xs.size == 2);
    tl_monotype *right = type->list.xs.v[1];
    str_build_cat(b, type_to_c_mono(self, right));
    str_build_cat(b, S(" (*"));
    str_build_cat(b, name);
    str_build_cat(b, S(") ("));

    assert(tl_tuple == type->list.xs.v[0]->tag);

    tl_monotype_sized params = type->list.xs.v[0]->list.xs;

    // if no params or context, exit early
    if (!params.size && !type->list.fvs.size) {
        str_build_cat(b, S("void"));
        goto done;
    }

    // generate lambda context argument for free variables
    if (type->list.fvs.size) {
        str ctx_name = context_name(self, type->list.fvs);
        str_build_cat(b, ctx_name);
        str_build_cat(b, S("*"));
        if (params.size) str_build_cat(b, S(", "));
    }

    for (u32 i = 0, n = params.size; i < n; ++i) {
        str_build_cat(b, type_to_c_mono(self, params.v[i]));
        if (i + 1 < n) str_build_cat(b, S(", "));
    }

done:
    str_build_cat(b, S(")"));
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

    // if no params or context, exit early
    if (!params.size && !arrow->list.fvs.size) {
        str_build_cat(&b, S("void"));
        return str_build_finish(&b);
    }

    // generate lambda context argument for free variables
    if (arrow->list.fvs.size) {
        str ctx_name = context_name(self, arrow->list.fvs);
        cat(self, ctx_name);
        cat_star(self);

        // always output a name for the context parameter, because we might be in a function definition.
        cat_sp(self);
        cat(self, S("tl_ctx"));

        if (params.size) cat_commasp(self);
    }

    for (u32 i = 0, n = params.size; i < n; ++i) {
        tl_monotype *arg = params.v[i];
        str          pname =
          (i < param_names.size) ? escape_c_keyword(self->transient, param_names.v[i]) : str_empty();
        if (tl_monotype_is_arrow(arg)) {
            build_arrow_to_c(self, &b, arg, pname);
        } else {
            str_build_cat(&b, type_to_c_mono(self, arg));
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
        str ctype = type_to_c_mono(self, type);
        return str_cat_3(self->transient, S("sizeof("), ctype, S(")"));
    } else if (node->named_application.n_arguments == 0) {
        fatal("sizeof: could not resolve type argument");
    }

    // single argument may be an expression or a type constructor
    if (1 != node->named_application.n_arguments) fatal("wrong number of arguments");
    ast_node const *arg = node->named_application.arguments[0];

    // // Note: The environment contains the most current type for a symbol argument.
    // tl_polytype *poly = arg->type;
    // if (ast_node_is_symbol(arg)) poly = tl_type_env_lookup(self->env, ast_node_str(arg));

    if (ast_node_is_nfa(arg)) {
        // type constructor
        tl_monotype *type = tl_type_registry_parse_type(self->registry, arg);
        if (!type) fatal("missing type");
        update_type(self, &type);

        str ctype = type_to_c_mono(self, type);
        return str_cat_3(self->transient, S("sizeof("), ctype, S(")"));
    } else {
        // expression
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
        str ctype = type_to_c_mono(self, type);
        return str_cat_3(self->transient, S("_Alignof("), ctype, S(")"));
    } else if (node->named_application.n_arguments == 0) {
        fatal("alignof: could not resolve type argument");
    }

    // single argument may be an expression or a type constructor
    if (1 != node->named_application.n_arguments) fatal("wrong number of arguments");
    ast_node const *arg  = node->named_application.arguments[0];

    tl_polytype    *poly = arg->type;
    if (ast_node_is_symbol(arg) && !tl_polytype_is_concrete(poly))
        poly = tl_type_env_lookup(self->env, ast_node_str(arg));

    if (ast_node_is_nfa(arg)) {
        // type constructor
        tl_monotype *type = tl_type_registry_parse_type(self->registry, arg);
        if (!type) fatal("missing type");
        update_type(self, &type);

        str ctype = type_to_c_mono(self, type);
        return str_cat_3(self->transient, S("_Alignof("), ctype, S(")"));
    } else {
        // expression - not support because MSVC makes us sad
        fatal("alignof(expression) not supported");
    }
}

static str tl_fatal(transpile *self, ast_node const *node, eval_ctx *ctx, void *extra) {
    (void)extra;
    (void)ctx;
    assert(ast_node_is_nfa(node));

    if (1 != node->named_application.n_arguments) fatal("wrong number of arguments");
    ast_node const *arg = node->named_application.arguments[0];
    if (ast_string != arg->tag) {
        // FIXME: report error
        fatal("expected string");
    }

    ctx->is_effective_void = 1;

    str msg                = ast_node_str(arg);

    return str_cat_3(self->transient, S("(fprintf(stderr, \""), msg, S("\\n\"), exit(1))"));
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

static void exit_error(char const *file, u32 line, char const *restrict fmt, ...) {
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
