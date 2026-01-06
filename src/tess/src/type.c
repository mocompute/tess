#include "type.h"
#include "ast.h"
#include "type_registry.h"

#include "alloc.h"
#include "array.h"
#include "hash.h"
#include "hashmap.h"
#include "infer.h"
#include "str.h"
#include "util.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define DEBUG_ENV 1

static void                      dbg(tl_type_env *, char const *restrict fmt, ...);
static void                      make_unary_tc(tl_type_registry *, str);
static void                      make_variable_arity_tc(tl_type_registry *, str);
static tl_type_constructor_def  *make_tc_def(tl_type_registry *, str);
static tl_type_constructor_inst *make_tc_inst_args(tl_type_registry *, tl_type_constructor_def *,
                                                   tl_monotype_sized);
static void                      make_nullary_inst(tl_type_registry *, str);
static tl_polytype              *make_generic_inst(tl_type_registry *, tl_type_constructor_inst *,
                                                   tl_type_variable_sized);
static void                      make_carray(tl_type_registry *);

static void                      mark_integer_type(tl_type_registry *, str);
static void                      mark_float_type(tl_type_registry *, str);

// -- type constructor --

static _Thread_local allocator *transient_allocator; // initialized by tl_type_registry_create

tl_type_registry *tl_type_registry_create(allocator *alloc, allocator *transient, tl_type_subs *subs) {
    tl_type_registry *self = alloc_malloc(alloc, sizeof *self);
    self->alloc            = alloc;
    self->transient        = transient;

    transient_allocator    = transient;

    self->subs             = subs;
    self->definitions      = map_new(self->alloc, str, tl_polytype *, 64);       // key: str
    self->specialized      = map_create(self->alloc, sizeof(tl_monotype *), 64); // key: registry_key
    self->type_aliases     = map_new(self->alloc, str, tl_polytype *, 64);

    // Basic types
    make_nullary_inst(self, S("Void"));
    make_nullary_inst(self, S("Int"));
    make_nullary_inst(self, S("Bool"));
    make_nullary_inst(self, S("Float"));
    make_nullary_inst(self, S("String"));

    // Ptr is unary type constructor
    make_unary_tc(self, S("Ptr"));

    // Union has variable arity
    make_variable_arity_tc(self, S("Union"));

    // C Integer types
    make_nullary_inst(self, S("CChar"));
    make_nullary_inst(self, S("CUnsignedChar"));
    make_nullary_inst(self, S("CSignedChar"));
    make_nullary_inst(self, S("CShort"));
    make_nullary_inst(self, S("CUnsignedShort"));
    make_nullary_inst(self, S("CInt"));
    make_nullary_inst(self, S("CUnsignedInt"));
    make_nullary_inst(self, S("CLong"));
    make_nullary_inst(self, S("CUnsignedLong"));
    make_nullary_inst(self, S("CLongLong"));
    make_nullary_inst(self, S("CUnsignedLongLong"));
    make_nullary_inst(self, S("CSize"));
    make_nullary_inst(self, S("CPtrDiff"));

    // Fixed size C Integers
    make_nullary_inst(self, S("CInt8"));
    make_nullary_inst(self, S("CUInt8"));
    make_nullary_inst(self, S("CInt16"));
    make_nullary_inst(self, S("CUInt16"));
    make_nullary_inst(self, S("CInt32"));
    make_nullary_inst(self, S("CUInt32"));
    make_nullary_inst(self, S("CInt64"));
    make_nullary_inst(self, S("CUInt64"));

    // Integer convertible types
    mark_integer_type(self, S("Int"));
    mark_integer_type(self, S("Bool"));
    mark_integer_type(self, S("CChar"));
    mark_integer_type(self, S("CUnsignedChar"));
    mark_integer_type(self, S("CSignedChar"));
    mark_integer_type(self, S("CInt"));
    mark_integer_type(self, S("CUnsignedInt"));
    mark_integer_type(self, S("CShort"));
    mark_integer_type(self, S("CUnsignedShort"));
    mark_integer_type(self, S("CLong"));
    mark_integer_type(self, S("CUnsignedLong"));
    mark_integer_type(self, S("CLongLong"));
    mark_integer_type(self, S("CUnsignedLongLong"));
    mark_integer_type(self, S("CSize"));
    mark_integer_type(self, S("CPtrDiff"));

    mark_integer_type(self, S("CInt8"));
    mark_integer_type(self, S("CUInt8"));
    mark_integer_type(self, S("CInt16"));
    mark_integer_type(self, S("CUInt16"));
    mark_integer_type(self, S("CInt32"));
    mark_integer_type(self, S("CUInt32"));
    mark_integer_type(self, S("CInt64"));
    mark_integer_type(self, S("CUInt64"));

    // C float types
    make_nullary_inst(self, S("CFloat"));
    make_nullary_inst(self, S("CDouble"));
    make_nullary_inst(self, S("CLongDouble"));

    // Float convertible types
    mark_float_type(self, S("Float"));
    mark_float_type(self, S("CFloat"));
    mark_float_type(self, S("CDouble"));
    mark_float_type(self, S("CLongDouble"));

    // CArray type
    make_carray(self);

    return self;
}

static tl_polytype *tl_type_constructor_create_ext(tl_type_registry *self, str name, str generic_name,
                                                   tl_type_variable_sized type_variables,
                                                   str_sized field_names, tl_monotype_sized field_types) {

    tl_type_constructor_def *def   = make_tc_def(self, name);
    def->field_names               = field_names;
    def->generic_name              = generic_name;

    tl_type_constructor_inst *inst = make_tc_inst_args(self, def, field_types);
    tl_polytype              *poly = make_generic_inst(self, inst, type_variables);

    return poly;
}

static tl_polytype *tl_type_constructor_create(tl_type_registry *self, str name,
                                               tl_type_variable_sized type_variables, str_sized field_names,
                                               tl_monotype_sized field_types) {

    return tl_type_constructor_create_ext(self, name, name, type_variables, field_names, field_types);
}

void tl_type_registry_insert(tl_type_registry *self, str name, tl_polytype *poly) {
    str_map_set_ptr(&self->definitions, name, poly);
}

void tl_type_registry_insert_mono(tl_type_registry *self, str name, tl_monotype *mono) {
    tl_polytype *poly = tl_monotype_generalize(self->alloc, mono);
    str_map_set_ptr(&self->definitions, name, poly);
}

tl_polytype *tl_type_constructor_def_create_ext(tl_type_registry *self, str name, str generic_name,
                                                tl_type_variable_sized type_variables,
                                                str_sized field_names, tl_monotype_sized field_types) {

    tl_polytype *poly =
      tl_type_constructor_create_ext(self, name, generic_name, type_variables, field_names, field_types);

    tl_type_registry_insert(self, name, poly);
    return poly;
}

tl_polytype *tl_type_constructor_def_create(tl_type_registry *self, str name,
                                            tl_type_variable_sized type_variables, str_sized field_names,
                                            tl_monotype_sized field_types) {

    return tl_type_constructor_def_create_ext(self, name, name, type_variables, field_names, field_types);
}

static void make_unary_tc(tl_type_registry *self, str name) {
    str_sized              empty = {0};

    tl_type_variable_array unary = {.alloc = self->alloc};
    tl_type_variable       tv    = tl_type_subs_fresh(self->subs);
    array_push(unary, tv);
    tl_monotype      *tv_type = tl_monotype_create_tv(self->alloc, tv);
    tl_monotype_array mt_arr  = {.alloc = self->alloc};
    array_push(mt_arr, tv_type);
    tl_type_constructor_def_create(self, name, (tl_type_variable_sized)sized_all(unary), empty,
                                   (tl_monotype_sized)sized_all(mt_arr));
}

static void make_variable_arity_tc(tl_type_registry *self, str name) {
    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};
    tl_monotype_sized      empty_mt = {0};

    tl_polytype           *poly     = tl_type_constructor_def_create(self, name, empty_tv, empty, empty_mt);
    poly->type->cons_inst->def->is_variable_args = 1;
}

static tl_type_constructor_def *make_tc_def(tl_type_registry *self, str name) {
    tl_type_constructor_def *def = alloc_malloc(self->alloc, sizeof *def);
    def->name                    = str_copy(self->alloc, name);
    def->generic_name            = str_copy(self->alloc, name);
    def->field_names             = (str_sized){0};
    def->is_variable_args        = 0;
    def->is_integer_convertible  = 0;
    def->is_float_convertible    = 0;
    return def;
}

static tl_type_constructor_inst *make_tc_inst_args(tl_type_registry *self, tl_type_constructor_def *def,
                                                   tl_monotype_sized args) {
    tl_type_constructor_inst *inst = alloc_malloc(self->alloc, sizeof *inst);
    inst->def                      = def;
    inst->args                     = args;
    inst->special_name             = str_empty();
    return inst;
}

static tl_polytype *make_generic_inst(tl_type_registry *self, tl_type_constructor_inst *inst,
                                      tl_type_variable_sized quantifiers) {
    tl_monotype *mono = tl_monotype_create_cons(self->alloc, inst);
    tl_polytype *poly = tl_polytype_absorb_mono(self->alloc, mono);
    poly->quantifiers = quantifiers;
    return poly;
}

static void make_nullary_inst(tl_type_registry *self, str name) {

    str_sized              empty    = {0};
    tl_type_variable_sized empty_tv = {0};
    tl_monotype_sized      empty_mt = {0};
    str                    blank    = str_empty();

    tl_type_constructor_def_create(self, name, empty_tv, empty, empty_mt);
    tl_type_registry_specialize(self, name, blank, empty_mt);
}

static void mark_integer_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    tl_monotype_set_integer_convertible(poly->type);
}

static void mark_float_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly || !tl_monotype_is_inst(poly->type)) fatal("logic error");
    tl_monotype_set_float_convertible(poly->type);
}

static void make_carray(tl_type_registry *self) {
    str       name                = S("CArray");
    str_sized field_names         = {.size = 2, .v = alloc_malloc(self->alloc, 2 * sizeof(str))};
    field_names.v[0]              = str_init(self->alloc, "type");
    field_names.v[1]              = str_init(self->alloc, "count");

    tl_monotype_sized field_types = {.size = 2, .v = alloc_malloc(self->alloc, 2 * sizeof(tl_monotype *))};
    field_types.v[0]              = tl_monotype_create_fresh_literal(self->alloc, self->subs);
    field_types.v[1]              = tl_type_registry_int(self);

    tl_type_variable_sized tvs    = {.size = 1, .v = alloc_malloc(self->alloc, sizeof(tl_type_variable))};
    tvs.v[0]                      = tl_monotype_tv(tl_monotype_literal_target(field_types.v[0]));

    tl_type_constructor_def_create(self, name, tvs, field_names, field_types);
}

// --

tl_monotype *tl_type_registry_instantiate(tl_type_registry *self, str name) {
    if (str_eq(name, S("Union"))) fatal("runtime error");
    if (str_eq(name, S("Ptr"))) fatal("runtime error");
    tl_monotype *type = null;
    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) return null;

    type = tl_polytype_instantiate(self->alloc, poly, self->subs);

    return type;
}

tl_monotype *tl_type_registry_instantiate_with(tl_type_registry *self, str name, tl_monotype_sized args) {
    // For use with Type literals only, e.g. instantiate a Point(a) as a Point(Int)
    tl_monotype *type = null;

    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) {
        // unknown type, possibly due to recursive types: return a weak type variable rather than null
        return tl_monotype_create_fresh_weak(self->subs);
    }

    u32 arity = poly->quantifiers.size;
    if (args.size != arity) fatal("runtime error");

    type = tl_polytype_instantiate_with(self->alloc, poly, args, self->subs);

    return type;
}

tl_monotype *tl_type_registry_instantiate_union(tl_type_registry *self, tl_monotype_sized args) {
    str          name = S("Union");
    tl_monotype *type = null;

    if (!args.size) fatal("runtime error");

    tl_polytype *poly = str_map_get_ptr(self->definitions, name);
    if (!poly) fatal("runtime error");

    type = tl_polytype_instantiate(self->alloc, poly, self->subs);
    assert(tl_monotype_is_inst(type));
    type->cons_inst->args = args;

    return type;
}

tl_monotype *tl_type_registry_instantiate_carray(tl_type_registry *self, tl_monotype *type, i32 count) {
    if (count < 0) return null;
    tl_polytype *poly          = str_map_get_ptr(self->definitions, S("CArray"));
    tl_monotype *inst          = tl_polytype_instantiate(self->alloc, poly, self->subs);
    inst->cons_inst->args.v[0] = type;
    inst->cons_inst->args.v[1] = tl_monotype_create_integer(self->alloc, count);
    return inst;
}

tl_monotype *tl_type_registry_get_cached_specialization(tl_type_registry *self, str name,
                                                        tl_monotype_sized args) {
    registry_key key = {.name_hash = str_hash64(name), .args_hash = tl_monotype_sized_hash64(0, args)};
    tl_monotype *out = map_get_ptr(self->specialized, &key, sizeof key);
    return out;
}

void tl_type_registry_type_alias_insert(tl_type_registry *self, str name, tl_polytype *type) {
    str_map_set_ptr(&self->type_aliases, name, type);
}

int tl_type_registry_is_nullary_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    return tl_polytype_is_nullary(poly);
}

int tl_type_registry_is_unary_type(tl_type_registry *self, str name) {
    tl_polytype *poly = tl_type_registry_get(self, name);
    return tl_polytype_is_unary(poly);
}

int tl_polytype_is_nullary(tl_polytype *poly) {
    if (!poly) return 0;
    if (tl_polytype_is_scheme(poly)) return 0;
    if (!tl_monotype_is_inst(poly->type)) return 0;
    if (poly->quantifiers.size) return 0;
    return 1;
}

int tl_polytype_is_unary(tl_polytype *poly) {
    if (!poly) return 0;
    return tl_monotype_is_unary(poly->type);
}

int tl_monotype_is_unary(tl_monotype *mono) {
    if (!mono) return 0;
    if (!tl_monotype_is_inst(mono)) return 0;
    if (1 != mono->cons_inst->args.size) return 0;
    return 1;
}

tl_type_registry_specialize_ctx tl_type_registry_specialize_begin(tl_type_registry *self, str name,
                                                                  str               special_name,
                                                                  tl_monotype_sized args) {

    // Don't specialize nullary type constructors: they should always exist with their canonical generic
    // name, because they're not generic.
    if (!args.size) return (tl_type_registry_specialize_ctx){.specialized = null};

    tl_monotype *type = null;
    registry_key key  = {.name_hash = str_hash64(name), .args_hash = tl_monotype_sized_hash64(0, args)};
    if ((type = map_get_ptr(self->specialized, &key, sizeof key)))
        return (tl_type_registry_specialize_ctx){.specialized = type, .is_existing = 1};

    tl_polytype *poly = tl_type_registry_get(self, name);
    if (!poly) return (tl_type_registry_specialize_ctx){.specialized = null};

    assert(tl_monotype_is_inst(poly->type));
    tl_type_constructor_def *def = poly->type->cons_inst->def;
    if (!def->is_variable_args) {
        u32 arity = poly->type->cons_inst->args.size;
        if (args.size != arity) fatal("runtime error");
    }

    type = tl_polytype_specialize_cons(self->alloc, poly, args, self, special_name);

    return (tl_type_registry_specialize_ctx){
      .specialized = type, .name = name, .special_name = special_name, .key = key};
}

tl_monotype *tl_type_registry_specialize_commit(tl_type_registry               *self,
                                                tl_type_registry_specialize_ctx ctx) {

    if (!str_is_empty(ctx.special_name)) {
        // Important: don't cache in specialised table if there is no special_name
        map_set_ptr(&self->specialized, &ctx.key, sizeof ctx.key, ctx.specialized);
        tl_type_constructor_def_create_ext(self, ctx.special_name, ctx.name, (tl_type_variable_sized){0},
                                           ctx.specialized->cons_inst->def->field_names,
                                           ctx.specialized->cons_inst->args);
    }

    return ctx.specialized;
}

tl_monotype *tl_type_registry_specialize(tl_type_registry *self, str name, str special_name,
                                         tl_monotype_sized args) {

    tl_type_registry_specialize_ctx ctx = tl_type_registry_specialize_begin(self, name, special_name, args);
    return tl_type_registry_specialize_commit(self, ctx);
}

tl_polytype *tl_type_registry_get(tl_type_registry *self, str name) {
    tl_polytype *poly = str_map_get_ptr(self->type_aliases, name);
    if (!poly) poly = str_map_get_ptr(self->definitions, name);
    return poly;
}

tl_polytype *tl_type_registry_get_nullary(tl_type_registry *self, str name) {
    tl_polytype *out = tl_type_registry_get(self, name);
    if (!tl_polytype_is_nullary(out)) out = null;
    return out;
}

tl_polytype *tl_type_registry_get_unary(tl_type_registry *self, str name) {
    tl_polytype *out = tl_type_registry_get(self, name);
    if (!tl_polytype_is_unary(out)) out = null;
    return out;
}

int tl_type_registry_exists(tl_type_registry *self, str name) {
    return str_map_contains(self->type_aliases, name) || str_map_contains(self->definitions, name);
}

tl_monotype *tl_type_registry_nil(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Void"));
}
tl_monotype *tl_type_registry_int(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Int"));
}
tl_monotype *tl_type_registry_float(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Float"));
}
tl_monotype *tl_type_registry_bool(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("Bool"));
}
tl_monotype *tl_type_registry_string(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("String"));
}
tl_monotype *tl_type_registry_char(tl_type_registry *self) {
    return tl_type_registry_instantiate(self, S("CChar"));
}

tl_polytype *tl_polytype_nil(allocator *alloc, tl_type_registry *self) {
    tl_monotype *nil = tl_type_registry_nil(self);
    return tl_polytype_absorb_mono(alloc, nil);
}

tl_monotype *tl_type_registry_ptr(tl_type_registry *self, tl_monotype *arg) {
    tl_monotype **arr = alloc_malloc(self->alloc, sizeof(void *));
    arr[0]            = arg;
    tl_monotype *out  = tl_type_registry_specialize(self, S("Ptr"), str_empty(),
                                                    (tl_monotype_sized){
                                                      .size = 1,
                                                      .v    = arr,
                                                   });
    assert(out);
    return out;
}

tl_monotype *tl_type_registry_ptr_any(tl_type_registry *self) {
    tl_monotype *any = tl_monotype_create_any(self->alloc);
    return tl_type_registry_ptr(self, any);
}

// -- parse_type

static tl_monotype *parse_type_specials(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                        ast_node const *node) {

    tl_monotype *mono = null;

    if (ast_node_is_symbol(node)) {
        str name = ast_node_str(node);
        if (str_eq(name, S("any"))) mono = tl_monotype_create_any(self->alloc);
        else if (str_eq(name, S("..."))) mono = tl_monotype_create_ellipsis(self->alloc);
        else if (str_eq(name, S("Type"))) {
            // Create a fresh type literal (target is a fresh type variable)
            assert(ctx->annotation_target);
            mono = tl_monotype_create_fresh_literal(self->alloc, self->subs);

            // Add to context type arguments
            str ta = ast_node_str(ctx->annotation_target);
            str_map_set_ptr(&ctx->type_arguments, ta, mono);
        }
    }

    return mono;
}

static tl_monotype *type_variable_sugar(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx,
                                        ast_node const *node) {
    tl_monotype *result = null;
    result              = tl_monotype_create_fresh_literal(self->alloc, self->subs);
    str ta              = ast_node_str(node);
    str_map_set_ptr(&ctx->type_arguments, ta, result);

    return tl_monotype_literal_target(result);
}

static tl_monotype *defer_parse(tl_type_registry *self, tl_type_registry_parse_type_ctx *ctx, str name) {
    if (str_hset_contains(ctx->in_progress, name) || str_map_contains(ctx->deferred_parse, name) ||
        (!str_map_get_ptr(ctx->type_arguments, name) && !tl_type_registry_get(self, name))) {

        // target cannot be parsed yet: create a placeholder type for it: Ptr(any)

        // fprintf(stderr, "parse_type: defer %s\n", str_cstr(&name));

        tl_monotype *placeholder = str_map_get_ptr(ctx->deferred_parse, name);
        if (!placeholder) {
            placeholder = tl_monotype_create_placeholder(self->alloc, name);
            str_map_set_ptr(&ctx->deferred_parse, name, placeholder);
        }

        // fprintf(stderr, "parse_type: defer %s : %p\n", str_cstr(&name), placeholder);
        return placeholder;
    }
    return null;
}

static tl_monotype *tl_type_registry_parse_type_(tl_type_registry                *self,
                                                 tl_type_registry_parse_type_ctx *ctx,
                                                 ast_node const                  *node) {

    tl_monotype *result = map_get_ptr(ctx->memoize, &node, sizeof(ast_node *));
    if (result) return result;

    if (ast_node_is_symbol(node)) {

        // Note: `(x : T)` is sugar for `(T: Type, x: T)`. `T` is assigned a type variable and added to ctx
        // as a type argument. Subsequent references to `T` will use the same type variable. The code often
        // confusingly refers to T as a 'type literal', because at the callsite, a literal type specifier
        // must be used, when T is explicity declared as a Type argument (`T : Type`).

        // is it a special: any, ..., Type
        result = parse_type_specials(self, ctx, node);
        if (result) goto top_success;

        // or is it a nullary literal: Int, Float, String, etc, including user type constructors
        str          name = ast_node_str(node);
        tl_polytype *poly = tl_type_registry_get_nullary(self, name);
        if (poly) {
            result = poly->type;
            goto top_success;
        }

        // or else is it a type argument previously defined?
        result = str_map_get_ptr(ctx->type_arguments, name);
        if (result) {
            // Note: type arguments will need to be handled in a context-sensitive way by the caller.

            if (tl_monotype_is_type_literal(result)) result = tl_monotype_literal_target(result);
            goto top_success;
        }

        // or else is it an annotated symbol? E.g. `count: Int` or `T: Type`
        if (node->symbol.annotation) {
            ast_node const *save   = ctx->annotation_target;
            ctx->annotation_target = node;
            result                 = tl_type_registry_parse_type_(self, ctx, node->symbol.annotation);
            ctx->annotation_target = save;

            // If the annotation produces nothing, and the annotation is a symbol, it's sugar for a type
            // variable.
            if (!result && ast_node_is_symbol(node->symbol.annotation)) {
                result = type_variable_sugar(self, ctx, node->symbol.annotation);
            }
        }

        goto top_success;
    }

    else if (ast_node_is_nfa(node)) {
        // a type constructor with args: Array(Float), Map(String, String), etc.
        // Note: `Ptr(T)` (if T is not a registered type name) is sugar for `Ptr(T: Type, T)`

        // Note: a nullary type should not be parsed as an nfa: Int() is not legal

        str name = ast_node_str(node->named_application.name);

        // Recursive types: check for indirection through Ptr (or any unary type) and defer it

        if (tl_type_registry_is_unary_type(self, name)) {

            // Note: returning null is valid, because this function may be called to try to parse things
            // which look like type literals for a while, but are actually type constructors.

            if (1 != node->named_application.n_arguments) {
                result = null; // possibly not a type literal
                goto top_success;
            }
            ast_node const *target      = node->named_application.arguments[0];

            ast_node const *target_name = null;
            if (ast_node_is_symbol(target)) target_name = target;
            else if (ast_node_is_nfa(target)) target_name = target->named_application.name;
            else {
                result = null;
                goto top_success;
            }

            str target_name_str = ast_node_str(target_name);

            // If the target is a symbol and not a utd in_progress, it must either be a nullary type or it
            // is sugar for a type variable, e.g a function in stdlib.tl which returns a `Ptr(T)`.
            if (ast_node_is_symbol(target)) {
                if (!str_hset_contains(ctx->in_progress, target_name_str)) {
                    tl_monotype *parsed = tl_type_registry_parse_type_(self, ctx, target);
                    if (!parsed) {
                        (void)type_variable_sugar(self, ctx, target);
                    }
                } else {
                    // If target name is an in_progress utd, we must defer the parse.
                    result = defer_parse(self, ctx, target_name_str);
                }
            } else {
                // Maybe defer non-symbol target
                result = defer_parse(self, ctx, target_name_str);
            }

            if (result) {
                tl_polytype *unary = tl_type_registry_get_unary(self, name);
                assert(tl_monotype_is_inst(unary->type));
                tl_monotype_sized args = {.v = alloc_malloc(self->alloc, sizeof(tl_monotype *)), .size = 1};
                args.v[0]              = result;
                tl_monotype *inst      = tl_polytype_instantiate_with(self->alloc, unary, args, self->subs);
                result                 = inst;
                goto top_success;
            }
        }

        tl_polytype *type_constructor = tl_type_registry_get(self, name);
        if (!type_constructor) {
            result = null;
            goto top_success;
        }

        if (!tl_monotype_is_inst(type_constructor->type)) {
            result = null;
            goto top_success;
        }
        if (tl_polytype_is_nullary(type_constructor)) {
            // invalid syntax
            result = null;
            goto top_success;
        };

        tl_monotype_array args  = {.alloc = self->alloc};
        ast_node_sized    nodes = ast_node_sized_from_ast_array_const(node);

        forall(i, nodes) {
            tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, nodes.v[i]);

            // If the type constructor argument produces nothing, and it's a symbol, it's sugar for a type
            // variable - not a type argument. Otherwise, it's an error
            if (!mono) {
                if (ast_node_is_symbol(nodes.v[i])) {
                    assert(ctx);
                    mono = type_variable_sugar(self, ctx, nodes.v[i]);
                } else {
                    result = null;
                    goto top_success;
                }
            }
            array_push_val(args, mono);
        }

        // Note: special case for parsing a Union(a, b, ...)
        if (str_eq(name, S("Union"))) {
            tl_monotype *mono =
              tl_type_registry_instantiate_union(self, (tl_monotype_sized)array_sized(args));
            result = mono;

            // FIXME: we don't support literal Union types.
        } else {
            result = tl_polytype_instantiate_with(self->alloc, type_constructor,
                                                  (tl_monotype_sized)array_sized(args), self->subs);
        }
    }

    else if (ast_node_is_arrow(node)) {
        // An arrow annotation: left is always a tuple in our system, even with 0 or 1 params, and right is
        // any type. Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)
        assert(ast_node_is_tuple(node->arrow.left));

        tl_monotype_array args  = {.alloc = self->alloc};
        ast_node_sized    nodes = ast_node_sized_from_ast_array_const(node->arrow.left);
        forall(i, nodes) {
            tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, nodes.v[i]);
            if (!mono) {
                assert(ctx);
                if (ast_node_is_symbol(nodes.v[i])) {
                    mono = type_variable_sugar(self, ctx, nodes.v[i]);
                } else {
                    result = null;
                    goto top_success;
                }
            }
            array_push_val(args, mono);
        }
        tl_monotype *left_mono =
          tl_monotype_create_tuple(self->alloc, (tl_monotype_sized)array_sized(args));

        tl_monotype *right_mono = tl_type_registry_parse_type_(self, ctx, node->arrow.right);
        if (!right_mono) {
            if (ast_node_is_symbol(node->arrow.right)) {
                right_mono = type_variable_sugar(self, ctx, node->arrow.right);
            } else {
                result = null;
                goto top_success;
            }
        }
        tl_monotype *arrow = tl_monotype_create_arrow(self->alloc, left_mono, right_mono);
        result             = arrow;
    }

    else if (ast_node_is_utd(node)) {
        str        name             = node->user_type_def.name->symbol.name;
        u32        n_type_arguments = node->user_type_def.n_type_arguments;
        u32        n_fields         = node->user_type_def.n_fields;
        ast_node **type_arguments   = node->user_type_def.type_arguments;
        ast_node **fields           = node->user_type_def.field_names;
        ast_node **annotations      = node->user_type_def.field_annotations;

        // Add name to in_progress
        str_hset_insert(&ctx->in_progress, name);

        // Add type arguments to parse context, and save for the type constructor.
        tl_type_variable_array type_argument_tvs = {.alloc = self->alloc};

        for (u32 i = 0, n = n_type_arguments; i < n; ++i) {
            assert(ast_node_is_symbol(type_arguments[i]));
            tl_monotype *mono = tl_monotype_create_fresh_literal(self->alloc, self->subs);
            str_map_set_ptr(&ctx->type_arguments, ast_node_str(type_arguments[i]), mono);

            array_push(type_argument_tvs, tl_monotype_literal_target(mono)->var);
        }

        str_array         field_names = {.alloc = self->alloc};
        tl_monotype_array field_types = {.alloc = self->alloc};
        array_reserve(field_names, n_fields);
        array_reserve(field_types, n_fields);
        for (u32 i = 0; i < n_fields; ++i) {
            assert(ast_node_is_symbol(fields[i]));
            array_push(field_names, fields[i]->symbol.name);

            // Note: enum types have no annotations
            if (annotations) {
                tl_monotype *mono = tl_type_registry_parse_type_(self, ctx, annotations[i]);
                if (!mono) goto utd_error; // TODO: better error

                array_push(field_types, mono);
            }
        }

        tl_polytype *poly = tl_type_constructor_create(
          self, name, (tl_type_variable_sized)array_sized(type_argument_tvs),
          (str_sized)array_sized(field_names), (tl_monotype_sized)array_sized(field_types));

        // fprintf(stderr, "parse_type: registered %s\n", str_cstr(&name));

        if (map_size(ctx->deferred_parse)) {
            // Resolve placeholders
            str_array keys = str_map_keys(self->transient, ctx->deferred_parse);
            forall(i, keys) {
                tl_monotype *placeholder = str_map_get_ptr(ctx->deferred_parse, keys.v[i]);

                if (tl_monotype_is_inst(poly->type)) {
                    if (str_eq(poly->type->cons_inst->def->generic_name, keys.v[i])) {
                        // mutate placeholder to resolved type: dependent types which retained the
                        // placeholder pointer will automatically get resolved type
                        // fprintf(stderr, "parse_type: defer resolve %s\n", str_cstr(&keys.v[i]));
                        *placeholder = *poly->type;
                        str_map_erase(ctx->deferred_parse, keys.v[i]);
                    }
                } else {
                    fatal("logic error");
                }
            }
        }

        result = tl_polytype_instantiate(self->alloc, poly, self->subs);
        // fprintf(stderr, "parse_type success: %s\n", str_cstr(&name));
        goto utd_success;

    utd_error:
        // fprintf(stderr, "parse_type error: %s\n", str_cstr(&name));
        result = null;

    utd_success:
        str_hset_remove(ctx->in_progress, name);
    }

top_success:
    map_set_ptr(&ctx->memoize, &node, sizeof(ast_node *), result);
    return result;
}

void tl_type_registry_parse_type_ctx_init(allocator *alloc, tl_type_registry_parse_type_ctx *ctx,
                                          hashmap *type_arguments) {
    *ctx = (tl_type_registry_parse_type_ctx){
      .memoize        = map_new(alloc, ast_node *, tl_monotype *, 64),
      .type_arguments = type_arguments ? type_arguments : map_new(alloc, str, tl_monotype *, 16),
      .deferred_parse = map_new(alloc, str, ast_node *, 8),
      .in_progress    = hset_create(alloc, 8),
    };
}

void tl_type_registry_parse_type_ctx_reset(tl_type_registry_parse_type_ctx *ctx) {
    // Note: does not reset deferred_parse, which is the whole point: to support single-pass fixups for
    // mutually recursive types.

    // Note: does not reset memoize
    map_reset(ctx->type_arguments);
    hset_reset(ctx->in_progress);
    ctx->annotation_target = null;
}

tl_monotype *tl_type_registry_parse_type(tl_type_registry *self, ast_node const *node) {
    // Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)
    tl_type_registry_parse_type_ctx ctx;
    tl_type_registry_parse_type_ctx_init(self->transient, &ctx, null);

    tl_monotype *result = tl_type_registry_parse_type_(self, &ctx, node);

    return result;
}

tl_monotype *tl_type_registry_parse_type_with_ctx(tl_type_registry *self, ast_node const *node,
                                                  tl_type_registry_parse_type_ctx *ctx) {
    // Example: "(T: Type, count: Int) -> Ptr(T)" => forall t0. (t0, Int) -> Ptr(t0)

    tl_monotype *result = tl_type_registry_parse_type_(self, ctx, node);

    return result;
}

tl_monotype *tl_type_registry_parse_type_out_ctx(tl_type_registry *self, ast_node const *node,
                                                 allocator *alloc, hashmap *outer_type_arguments,
                                                 tl_type_registry_parse_type_ctx *out_ctx) {
    // alloc is used to allocate the parse context's buffers/maps, and the struct is copied into
    // out_ctx.
    assert(out_ctx);
    tl_type_registry_parse_type_ctx ctx;
    tl_type_registry_parse_type_ctx_init(alloc, &ctx, outer_type_arguments);
    tl_monotype *result = tl_type_registry_parse_type_(self, &ctx, node);
    *out_ctx            = ctx;

    return result;
}

// -- type environment --

tl_type_env *tl_type_env_create(allocator *alloc) {
    tl_type_env *self = alloc_malloc(alloc, sizeof *self);
    self->alloc       = alloc;
    self->map         = map_create(self->alloc, sizeof(tl_polytype *), 64); // key: str
    self->verbose     = 0;

    return self;
}

void tl_type_env_insert(tl_type_env *self, str name, tl_polytype *type) {
#if DEBUG_ENV
    str type_str = tl_polytype_to_string(transient_allocator, type);
    dbg(self, "insert %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));
#endif
    str_map_set_ptr(&self->map, name, type);
}

void tl_type_env_insert_mono(tl_type_env *self, str name, tl_monotype *type) {
    tl_polytype *poly = tl_polytype_absorb_mono(self->alloc, type);
#if DEBUG_ENV
    str type_str = tl_polytype_to_string(transient_allocator, poly);
    dbg(self, "insert_mono %.*s :  %.*s", str_ilen(name), str_buf(&name), str_ilen(type_str),
        str_buf(&type_str));
#endif
    str_map_set_ptr(&self->map, name, poly);
}

tl_polytype *tl_type_env_lookup(tl_type_env *self, str name) {
    return str_map_get_ptr(self->map, name);
}

int tl_type_env_check_missing_fvs(tl_type_env *self, missing_fv_cb cb, void *user) {
    int              error = 0;

    hashmap_iterator iter  = {0};
    while (map_iter(self->map, &iter)) {
        str          name = str_init_n(transient_allocator, iter.key_ptr, iter.key_size);
        tl_polytype *type = *(tl_polytype **)iter.data;

        str_sized    fvs  = tl_monotype_fvs(type->type);
        forall(i, fvs) {
            if (!str_map_contains(self->map, fvs.v[i])) {
                if (cb) cb(user, name, fvs.v[i]);
                ++error;
            }
        }
    }
    return error;
}

void tl_type_env_remove_unknown_symbols(tl_type_env *self, hashmap *known) {
    str_array        remove = {.alloc = transient_allocator};

    hashmap_iterator iter   = {0};
    while (map_iter(self->map, &iter)) {
        str name = str_init_n(transient_allocator, iter.key_ptr, iter.key_size);
        if (!str_hset_contains(known, name)) array_push(remove, name);
    }

    forall(i, remove) {
        // Don't remove c_ symbols
        if (is_c_symbol(remove.v[i])) continue;

        str_map_erase(self->map, remove.v[i]);
        // dbg(self, "tl_type_env_remove_unknown_symbols: removing '%s'", str_cstr(&remove.v[i]));
    }
}

// -- polytype --

tl_polytype *tl_polytype_absorb_mono(allocator *alloc, tl_monotype *mono) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->quantifiers = (tl_type_variable_sized){0};
    self->type        = mono;
    return self;
}

tl_polytype *tl_polytype_create(allocator *alloc, tl_type_variable_sized quantifiers, tl_monotype *mono) {
    tl_polytype *self = tl_polytype_absorb_mono(alloc, mono);
    self->quantifiers = quantifiers;
    return self;
};

tl_polytype *tl_polytype_create_qv(allocator *alloc, tl_type_variable qv) {
    tl_polytype *self = alloc_malloc(alloc, sizeof *self);
    self->type        = tl_monotype_create_tv(alloc, qv);
    self->quantifiers =
      (tl_type_variable_sized){.size = 1, .v = alloc_malloc(alloc, sizeof(tl_type_variable))};
    self->quantifiers.v[0] = qv;
    return self;
}

tl_polytype *tl_polytype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *mono = tl_monotype_create_tv(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype *tl_polytype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *mono = tl_monotype_create_weak(alloc, tv);
    return tl_polytype_absorb_mono(alloc, mono);
}

tl_polytype *tl_polytype_create_fresh_weak(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_weak(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_create_fresh_qv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_qv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_create_fresh_tv(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_create_tv(alloc, tl_type_subs_fresh(subs));
}

tl_polytype *tl_polytype_create_fresh_literal(allocator *alloc, tl_type_subs *subs) {
    return tl_polytype_absorb_mono(alloc, tl_monotype_create_fresh_literal(alloc, subs));
}

tl_polytype *tl_polytype_create_literal_with(allocator *alloc, tl_monotype *target) {
    tl_monotype *literal = tl_monotype_create_literal(alloc, target);
    return tl_polytype_absorb_mono(alloc, literal);
}

tl_polytype *tl_polytype_clone(allocator *alloc, tl_polytype *orig) {
    tl_polytype *clone = alloc_malloc(alloc, sizeof *clone);
    clone->quantifiers = orig->quantifiers;
    clone->type        = tl_monotype_clone(alloc, orig->type);
    return clone;
}

tl_polytype *tl_polytype_clone_mono(allocator *alloc, tl_monotype *mono) {
    return tl_polytype_absorb_mono(alloc, tl_monotype_clone(alloc, mono));
}

void tl_polytype_list_append(allocator *alloc, tl_polytype *lhs, tl_polytype *rhs) {
    if (rhs->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, lhs->quantifiers.size + rhs->quantifiers.size);
        array_push_many(arr, lhs->quantifiers.v, lhs->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, rhs->quantifiers) array_set_insert(arr, rhs->quantifiers.v[i]);
        alloc_free(alloc, lhs->quantifiers.v);
        lhs->quantifiers.size = arr.size;
        lhs->quantifiers.v    = arr.v;
    }

    tl_monotype *left  = lhs->type;
    tl_monotype *right = rhs->type;
    assert(tl_arrow == left->tag);

    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, left->list.xs.size);
    array_push_many(arr, left->list.xs.v, left->list.xs.size);

    switch (right->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_literal:
    case tl_var:
    case tl_weak:
    case tl_cons_inst:   array_push(arr, right); break;
    case tl_arrow:
    case tl_tuple:       array_push_many(arr, right->list.xs.v, right->list.xs.size); break;
    }

    array_shrink(arr);
    alloc_free(alloc, left->list.xs.v);
    left->list.xs = (tl_monotype_sized)sized_all(arr);
}

void tl_polytype_merge_quantifiers(allocator *alloc, tl_polytype *self, tl_polytype *in) {
    if (in->quantifiers.size) {
        tl_type_variable_array arr = {.alloc = alloc};
        array_reserve(arr, self->quantifiers.size + in->quantifiers.size);
        array_push_many(arr, self->quantifiers.v, self->quantifiers.size);

        // merge rhs quants into lhs
        forall(i, in->quantifiers) array_set_insert(arr, in->quantifiers.v[i]);
        array_shrink(arr);

        alloc_free(alloc, self->quantifiers.v);
        self->quantifiers.size = arr.size;
        self->quantifiers.v    = arr.v;
    }
}

void tl_polytype_merge_quantifiers_sized(allocator *alloc, tl_polytype *self, tl_polytype_sized arr) {
    tl_type_variable_array tv_array = {.alloc = alloc};
    u32                    sum      = 0;
    forall(i, arr) sum += arr.v[i]->quantifiers.size;
    array_reserve(tv_array, self->quantifiers.size + sum);
    array_push_many(tv_array, self->quantifiers.v, self->quantifiers.size);

    forall(i, arr) {
        tl_polytype *in = arr.v[i];
        forall(i, in->quantifiers) array_set_insert(tv_array, in->quantifiers.v[i]);
    }

    alloc_free(alloc, self->quantifiers.v);
    self->quantifiers.size = tv_array.size;
    self->quantifiers.v    = tv_array.v;
}

static void replace_tv(tl_monotype *self, tl_type_subs *subs, hashmap *map, hashmap **seen) {
    if (!self || ptr_hset_contains(*seen, self)) return;
    ptr_hset_insert(seen, self);

    // map: tv -> tv

    // Replaces all type variables in self with the matching type variable in map. Used during instantiation
    // to convert quantified type variables to unquantified type variables.

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    break;

    case tl_var:         {
        tl_type_variable *replace = map_get(map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
    } break;

    case tl_weak: {
        // Every weak tv gets a fresh tv on instantiation.
        tl_type_variable *replace = map_get(map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
        else {
            tl_type_variable fresh = tl_type_subs_fresh(subs);
            map_set(seen, &self->var, sizeof self->var, &fresh);
            self->var = fresh;
        }

    } break;

    case tl_literal:
        //
        replace_tv(self->literal, subs, map, seen);
        break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) replace_tv(arr.v[i], subs, map, seen);
    } break;
    }
}

static void replace_tv_mono(tl_monotype *self, tl_type_subs *subs, hashmap *map, hashmap **seen) {
    if (!self || ptr_hset_contains(*seen, self)) return;
    ptr_hset_insert(seen, self);

    // map: tv -> monotype

    // Replaces all type variables in self with the matching monotypes in map. Used during instantiation
    // to convert quantified type variables to arbitrary monotypes.

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_var:         break;

    case tl_weak:        {
        // Every weak tv gets a fresh tv on instantiation.
        tl_type_variable *replace = map_get(map, &self->var, sizeof self->var);
        if (replace) self->var = *replace;
        else {
            tl_type_variable fresh = tl_type_subs_fresh(subs);
            map_set(seen, &self->var, sizeof self->var, &fresh);
            self->var = fresh;
        }
    } break;

    case tl_literal:
        //
        if (tl_monotype_is_tv(self->literal) &&
            map_contains(map, &self->literal->var, sizeof(tl_type_variable)))
            self->literal = map_get_ptr(map, &self->literal->var, sizeof(tl_type_variable));
        else replace_tv_mono(self->literal, subs, map, seen);
        break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;

        forall(i, arr) {
            tl_monotype *mono = arr.v[i];
            if (tl_monotype_is_tv(mono) && map_contains(map, &mono->var, sizeof(tl_type_variable)))
                arr.v[i] = map_get_ptr(map, &mono->var, sizeof(tl_type_variable));
            else replace_tv_mono(arr.v[i], subs, map, seen);
        }
    } break;
    }
}

tl_monotype *tl_polytype_instantiate(allocator *alloc, tl_polytype *self, tl_type_subs *subs) {

    tl_monotype *fresh  = tl_monotype_clone(alloc, self->type);

    hashmap     *q_to_t = map_create(transient_allocator, sizeof(tl_type_variable), 8);

    forall(i, self->quantifiers) {
        // make a fresh variable for each quantified type variable
        tl_type_variable tv = tl_type_subs_fresh(subs);
        map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &tv);
    }

    hashmap *seen = hset_create(transient_allocator, 8);
    replace_tv(fresh, subs, q_to_t, &seen);

    return fresh;
}

tl_monotype *tl_polytype_instantiate_with(allocator *alloc, tl_polytype *self, tl_monotype_sized args,
                                          tl_type_subs *subs) {
    // Instantiate a quantified polytype with specific monotypes, which may or may not be type variables.

    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);
    if (self->quantifiers.size != args.size) fatal("logic error");

    hashmap *q_to_t = map_create(transient_allocator, sizeof(tl_monotype *), args.size);

    forall(i, self->quantifiers) {
        map_set(&q_to_t, &self->quantifiers.v[i], sizeof(tl_type_variable), &args.v[i]);
    }

    hashmap *seen = hset_create(transient_allocator, 8);
    if (tl_monotype_is_tv(fresh) && map_contains(q_to_t, &fresh->var, sizeof fresh->var)) {
        fresh = map_get_ptr(q_to_t, &fresh->var, sizeof fresh->var);
        if (!fresh) fatal("unreachable");
    } else {
        replace_tv_mono(fresh, subs, q_to_t, &seen);
    }

#ifndef NDEBUG
    // assert the argument for a unary type maintains its pointer identity
    if (1 == fresh->cons_inst->args.size) assert(args.v[0] == fresh->cons_inst->args.v[0]);
#endif

    return fresh;
}

tl_monotype *tl_polytype_specialize(allocator *alloc, tl_polytype *self, tl_monotype_sized args) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized *inst = &fresh->cons_inst->args;
        if (args.size != inst->size) fatal("logic error");
        forall(i, *inst) inst->v[i] = args.v[i];
    }

    return fresh;
}

tl_monotype *tl_polytype_specialize_cons(allocator *alloc, tl_polytype *self, tl_monotype_sized args,
                                         tl_type_registry *registry, str special_name) {
    tl_monotype *fresh = tl_monotype_clone(alloc, self->type);

    // ignores quantifiers
    if (tl_cons_inst == fresh->tag) {
        tl_monotype_sized *inst             = &fresh->cons_inst->args;
        int                is_variable_args = fresh->cons_inst->def->is_variable_args;
        if (!is_variable_args) {
            if (args.size != inst->size) fatal("logic error");
        }

        // specialise cons arguments using registry
        forall(i, args) {
            if (tl_monotype_is_inst(args.v[i])) {
                str               name      = args.v[i]->cons_inst->def->name;
                tl_monotype_sized inst_args = args.v[i]->cons_inst->args;
                tl_monotype      *replace =
                  tl_type_registry_get_cached_specialization(registry, name, inst_args);
                if (replace) args.v[i] = replace;
            }
        }

        if (!is_variable_args) {
            forall(i, *inst) inst->v[i] = args.v[i];
        } else {
            *inst = args;
        }
        fresh->cons_inst->special_name = str_copy(alloc, special_name);
    }

    return fresh;
}

static void generalize(tl_monotype *self, tl_type_variable_array *quant, hashmap **seen) {
    if (!self || ptr_hset_contains(*seen, self)) return;
    ptr_hset_insert(seen, self);

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    break;

    case tl_var:
        //
        array_set_insert(*quant, self->var);
        break;

    case tl_weak: break;

    case tl_literal:
        //
        generalize(self->literal, quant, seen);
        break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) generalize(arr.v[i], quant, seen);

    } break;
    }
}

void tl_polytype_generalize(tl_polytype *self, tl_type_env *env, tl_type_subs *subs) {

    hashmap               *seen  = hset_create(transient_allocator, 8);
    tl_type_variable_array quant = {.alloc = transient_allocator};
    generalize(self->type, &quant, &seen);
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
    // leaks prior array, if any

    // instantiate to get fresh vars, then generalise again using the fresh vars
    self->type = tl_polytype_instantiate(env->alloc, self, subs);
    quant      = (tl_type_variable_array){.alloc = env->alloc};
    hset_reset(seen);
    generalize(self->type, &quant, &seen);
    self->quantifiers.size = quant.size;
    self->quantifiers.v    = quant.v;
}

tl_monotype *tl_polytype_concrete(tl_polytype *self) {
    if (!tl_polytype_is_concrete(self)) fatal("runtime error");
    return self->type;
}

tl_polytype *tl_monotype_generalize(allocator *alloc, tl_monotype *mono) {
    tl_type_variable_array quantifiers = {.alloc = alloc};
    hashmap               *seen        = hset_create(transient_allocator, 8);
    generalize(mono, &quantifiers, &seen);
    return tl_polytype_create(alloc, (tl_type_variable_sized)array_sized(quantifiers), mono);
}

// -- monotype --

tl_monotype *tl_monotype_create_any(allocator *alloc) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_any};
    return self;
}

tl_monotype *tl_monotype_create_placeholder(allocator *alloc, str name) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_placeholder, .placeholder = name};
    return self;
}

tl_monotype *tl_monotype_create_ellipsis(allocator *alloc) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_ellipsis};
    return self;
}

tl_monotype *tl_monotype_create_tv(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_var, .var = tv};
    return self;
}

tl_monotype *tl_monotype_create_integer(allocator *alloc, i32 integer) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_integer, .integer = integer};
    return self;
}

tl_monotype *tl_monotype_create_literal(allocator *alloc, tl_monotype *target) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_literal, .literal = target};
    return self;
}

tl_monotype *tl_monotype_create_fresh_tv(tl_type_subs *subs) {
    tl_type_variable tv = tl_type_subs_fresh(subs);
    return tl_monotype_create_tv(subs->data.alloc, tv);
}

tl_monotype *tl_monotype_create_fresh_literal(allocator *alloc, tl_type_subs *subs) {
    tl_type_variable tv     = tl_type_subs_fresh(subs);
    tl_monotype     *target = tl_monotype_create_tv(alloc, tv);

    tl_monotype     *self   = alloc_malloc(alloc, sizeof *self);
    *self                   = (tl_monotype){.tag = tl_literal, .literal = target};
    return self;
}

tl_monotype *tl_monotype_create_weak(allocator *alloc, tl_type_variable tv) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_weak, .var = tv};
    return self;
}

nodiscard tl_monotype *tl_monotype_create_fresh_weak(tl_type_subs *self) {
    tl_type_variable tv = tl_type_subs_fresh(self);
    return tl_monotype_create_weak(self->data.alloc, tv);
}

tl_monotype *tl_monotype_create_arrow(allocator *alloc, tl_monotype *lhs, tl_monotype *rhs) {
    tl_monotype      *left  = tl_monotype_clone(alloc, lhs);
    tl_monotype      *right = tl_monotype_clone(alloc, rhs);
    tl_monotype_array arr   = {.alloc = alloc};
    assert(left);
    if (right) {
        array_reserve(arr, 2);
        array_push(arr, left);
        array_push(arr, right);
    } else {
        array_push(arr, left);
    }
    return tl_monotype_create_list(alloc, (tl_monotype_sized)array_sized(arr));
}

tl_monotype *tl_monotype_create_list(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_arrow, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_tuple(allocator *alloc, tl_monotype_sized xs) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_tuple, .list = {.xs = xs}};
    return self;
}

tl_monotype *tl_monotype_create_cons(allocator *alloc, tl_type_constructor_inst *cons) {
    tl_monotype *self = alloc_malloc(alloc, sizeof *self);
    *self             = (tl_monotype){.tag = tl_cons_inst, .cons_inst = cons};
    return self;
}

static tl_monotype *tl_monotype_clone_(allocator *alloc, tl_monotype *orig, hashmap **mapping) {
    if (!orig) return null;

    // Note: never clone a placeholder type, because they need to retain their pointer identity.
    if (tl_placeholder == orig->tag) return orig;

    tl_monotype *found = map_get_ptr(*mapping, &orig, sizeof(void *));
    if (found) return found;

    tl_monotype *clone = alloc_malloc(alloc, sizeof *clone);
    map_set_ptr(mapping, &orig, sizeof(void *), clone);

    switch (orig->tag) {
    case tl_placeholder: fatal("unreachable");
    case tl_any:         *clone = (tl_monotype){.tag = tl_any}; return clone;
    case tl_ellipsis:    *clone = (tl_monotype){.tag = tl_ellipsis}; return clone;
    case tl_integer:     *clone = (tl_monotype){.tag = tl_integer, .integer = orig->integer}; return clone;
    case tl_var:         *clone = (tl_monotype){.tag = tl_var, .var = orig->var}; return clone;
    case tl_weak:        *clone = (tl_monotype){.tag = tl_weak, .var = orig->var}; return clone;

    case tl_cons_inst:
        // copy the tl_type_constructor_inst struct
        *clone =
          (tl_monotype){.tag = tl_cons_inst, .cons_inst = alloc_malloc(alloc, sizeof *clone->cons_inst)};
        *clone->cons_inst = *orig->cons_inst;

        // clone the args list
        clone->cons_inst->args         = tl_monotype_sized_clone(alloc, orig->cons_inst->args, mapping);
        clone->cons_inst->special_name = str_copy(alloc, orig->cons_inst->special_name);

        break;

    case tl_literal:
        *clone =
          (tl_monotype){.tag = tl_literal, .literal = tl_monotype_clone_(alloc, orig->literal, mapping)};
        break;

    case tl_arrow:
    case tl_tuple:
        *clone          = (tl_monotype){.tag  = orig->tag,
                                        .list = {.xs = tl_monotype_sized_clone(alloc, orig->list.xs, mapping)}};
        clone->list.fvs = orig->list.fvs; // shallow copy
        break;
    }

    return clone;
}

tl_monotype *tl_monotype_clone(allocator *alloc, tl_monotype *orig) {
    hashmap     *mapping = map_create_ptr(transient_allocator, 32);
    tl_monotype *out     = tl_monotype_clone_(alloc, orig, &mapping);
    return out;
}

int tl_monotype_is_concrete_(tl_monotype *self, hashmap **seen) {
    if (!self) return 0;
    if (ptr_hset_contains(*seen, self)) return 1;
    ptr_hset_insert(seen, self);

    switch (self->tag) {

    case tl_placeholder:
    case tl_var:
    case tl_ellipsis:    return 0;

    case tl_integer:
    case tl_any:
        //
        return 1;

    case tl_weak:
        // consider weak type variables as concrete, which is *usually* what type inference client wants.
        // There are _no_weak variants, otherwise.
        return 1;

    case tl_cons_inst:
        forall(i, self->cons_inst->args) if (!tl_monotype_is_concrete_(self->cons_inst->args.v[i],
                                                                       seen)) return 0;
        return 1;
    case tl_arrow:
    case tl_tuple: {
        forall(i, self->list.xs) if (!tl_monotype_is_concrete_(self->list.xs.v[i], seen)) return 0;
        return 1;
    }

    case tl_literal:
        //
        return tl_monotype_is_concrete_(self->literal, seen);
    }
    fatal("unreachable");
}

int tl_monotype_is_concrete(tl_monotype *self) {
    hashmap *seen = hset_create(transient_allocator, 32);
    int      res  = tl_monotype_is_concrete_(self, &seen);
    return res;
}

int tl_monotype_arrow_is_concrete(tl_monotype *self) {
    // If arrow type is concrete on its arguments, and only polymorphic on its return type, we can consider
    // it concrete.
    if (!tl_monotype_is_arrow(self)) return 0;
    tl_monotype *params = self->list.xs.v[0];
    if (!tl_monotype_is_tuple(params)) fatal("runtime error");
    return tl_monotype_is_concrete(params);
}

int tl_monotype_is_weak_(tl_monotype *self, hashmap **seen) {
    if (!self) return 0;
    if (ptr_hset_contains(*seen, self)) return 0;
    ptr_hset_insert(seen, self);

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_var:
    case tl_ellipsis:    return 0;

    case tl_weak:        return 1;

    case tl_cons_inst:
        forall(i,
               self->cons_inst->args) if (tl_monotype_is_weak_(self->cons_inst->args.v[i], seen)) return 1;
        return 0;
    case tl_arrow:
    case tl_tuple: {
        forall(i, self->list.xs) if (tl_monotype_is_weak_(self->list.xs.v[i], seen)) return 1;
        return 0;
    }

    case tl_literal:
        //
        return tl_monotype_is_weak_(self->literal, seen);
    }
    fatal("unreachable");
}

int tl_monotype_is_weak_deep(tl_monotype *self) {
    hashmap *seen = hset_create(transient_allocator, 32);
    int      res  = tl_monotype_is_weak_(self, &seen);
    return res;
}

int tl_monotype_sized_is_concrete(tl_monotype_sized arr) {
    forall(i, arr) if (!tl_monotype_is_concrete(arr.v[i])) return 0;
    return 1;
}

int tl_monotype_sized_is_concrete_no_weak(tl_monotype_sized arr) {
    forall(i, arr) if (!tl_monotype_is_concrete_no_weak(arr.v[i])) return 0;
    return 1;
}

int tl_monotype_is_concrete_no_arrow(tl_monotype *self) {
    return self && tl_cons_inst == self->tag && tl_monotype_is_concrete(self);
}

int tl_monotype_is_concrete_no_weak(tl_monotype *self) {
    return tl_monotype_is_concrete(self) && !tl_monotype_is_weak_deep(self);
}

int tl_monotype_is_any(tl_monotype *self) {
    return self && tl_any == self->tag;
}
int tl_monotype_is_integer(tl_monotype *self) {
    return self && tl_integer == self->tag;
}
int tl_monotype_is_weak(tl_monotype *self) {
    return self && tl_weak == self->tag;
}
int tl_monotype_is_ellipsis(tl_monotype *self) {
    return self && tl_ellipsis == self->tag;
}

int tl_monotype_is_arrow(tl_monotype *self) {
    return self && tl_arrow == self->tag;
}

int tl_monotype_is_inst_of(tl_monotype *self, str name) {
    return self && tl_cons_inst == self->tag && str_eq(self->cons_inst->def->generic_name, name);
}

int tl_monotype_is_void(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Void"));
}

int tl_monotype_is_list(tl_monotype *self) {
    return self && tl_arrow == self->tag;
}

int tl_monotype_is_inst(tl_monotype *self) {
    return self && tl_cons_inst == self->tag;
}

int tl_monotype_is_inst_specialized(tl_monotype *self) {
    return tl_monotype_is_inst(self) && !str_is_empty(self->cons_inst->special_name);
}

int tl_monotype_is_enum(tl_monotype *self) {
    // Enum type: has field names but has no instance args
    return tl_monotype_is_inst(self) && self->cons_inst->def->field_names.size &&
           !self->cons_inst->args.size;
}

int tl_monotype_is_tuple(tl_monotype *self) {
    return self && tl_tuple == self->tag;
}

int tl_monotype_is_tv(tl_monotype *self) {
    return self && tl_var == self->tag;
}

int tl_monotype_is_string(tl_monotype *self) {
    return self && tl_monotype_is_inst_of(self, S("String"));
}

int tl_monotype_is_ptr_to_char(tl_monotype *self) {
    if (!self || !tl_monotype_is_ptr(self)) return 0;
    tl_monotype *target = tl_monotype_ptr_target(self);
    return tl_monotype_is_inst_of(target, S("CChar"));
}

int tl_monotype_is_ptr_to_tv(tl_monotype *self) {
    if (!self || !tl_monotype_is_ptr(self)) return 0;
    tl_monotype *target = tl_monotype_ptr_target(self);
    return tl_monotype_is_tv(target);
}

int tl_monotype_is_ptr(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Ptr"));
}

int tl_monotype_is_ptr_or_null(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("PtrOrNull"));
}
int tl_monotype_is_union(tl_monotype *self) {
    return tl_monotype_is_inst_of(self, S("Union"));
}
int tl_monotype_has_ptr(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) return 0;
    if (tl_monotype_is_union(self)) {
        forall(i, self->cons_inst->args) {
            if (tl_monotype_has_ptr(self->cons_inst->args.v[i])) return 1;
        }
        return 0;
    } else {
        return tl_monotype_is_ptr(self) || tl_monotype_is_ptr_or_null(self);
    }
}

int tl_monotype_is_type_literal(tl_monotype *self) {
    return (tl_literal == self->tag);
}

int tl_monotype_is_integer_convertible(tl_monotype *self) {
    return tl_monotype_is_inst(self) && self->cons_inst->def->is_integer_convertible;
}
int tl_monotype_is_float_convertible(tl_monotype *self) {
    return tl_monotype_is_inst(self) && self->cons_inst->def->is_float_convertible;
}

void tl_monotype_set_integer_convertible(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) fatal("logic error");
    self->cons_inst->def->is_integer_convertible = 1;
}

void tl_monotype_set_float_convertible(tl_monotype *self) {
    if (!tl_monotype_is_inst(self)) fatal("logic error");
    self->cons_inst->def->is_float_convertible = 1;
}

i32 tl_monotype_integer(tl_monotype *self) {
    if (tl_monotype_is_integer(self)) return self->integer;
    fatal("runtime error");
}
tl_type_variable tl_monotype_tv(tl_monotype *self) {
    if (tl_monotype_is_tv(self)) return self->var;
    fatal("runtime error");
}

int tl_polytype_is_scheme(tl_polytype *poly) {
    return poly->quantifiers.size != 0;
}

int tl_polytype_is_concrete(tl_polytype *self) {
    return !tl_polytype_is_scheme(self) && tl_monotype_is_concrete(self->type);
}

int tl_polytype_is_type_constructor(tl_polytype *self) {
    return self && tl_monotype_is_inst(self->type);
}

int tl_polytype_type_constructor_has_field(tl_polytype *self, str name) {
    assert(tl_polytype_is_type_constructor(self));
    tl_type_constructor_def *def = self->type->cons_inst->def;
    return str_array_contains_one(def->field_names, name);
}

i32 tl_monotype_type_constructor_field_index(tl_monotype *self, str name) {
    assert(tl_monotype_is_inst(self));
    tl_type_constructor_def *def   = self->cons_inst->def;
    i32                      found = -1;
    forall(i, def->field_names) {
        if (str_eq(name, def->field_names.v[i])) {
            if (i > INT32_MAX) fatal("overflow");
            found = (i32)i;
            break;
        }
    }
    return found;
}

tl_monotype *tl_monotype_unary_target(tl_monotype *self) {
    if (!self || !tl_monotype_is_inst(self) || 1 != self->cons_inst->args.size) return null;
    return self->cons_inst->args.v[0];
}

tl_monotype *tl_monotype_ptr_target(tl_monotype *self) {
    if (tl_monotype_is_ptr(self)) {
        assert(self->cons_inst->args.size == 1);
        return self->cons_inst->args.v[0];
    } else if (tl_monotype_is_ptr_or_null(self)) {
        assert(self->cons_inst->args.size == 2);
        // first argument is Ptr
        return tl_monotype_ptr_target(self->cons_inst->args.v[0]);
    } else if (tl_monotype_is_union(self)) {
        forall(i, self->cons_inst->args) {
            tl_monotype *arg = self->cons_inst->args.v[i];
            if (tl_monotype_is_ptr(arg)) return tl_monotype_ptr_target(arg);
        }
        fatal("runtime error");
    }

    else
        fatal("unreachable");
}

void tl_monotype_ptr_set_target(tl_monotype *ptr, tl_monotype *target) {
    assert(tl_monotype_is_ptr(ptr));
    assert(target);
    ptr->cons_inst->args.v[0] = target;
    assert(tl_monotype_ptr_target(ptr) == target);
}

tl_monotype *tl_monotype_literal_target(tl_monotype *self) {
    if (tl_monotype_is_type_literal(self)) return self->literal;
    fatal("logic error");
}

tl_monotype *tl_monotype_arrow_args(tl_monotype *self) {
    assert(tl_monotype_is_list(self));
    assert(2 == self->list.xs.size);
    tl_monotype *args = self->list.xs.v[0];
    assert(tl_monotype_is_tuple(args));
    return args;
}

tl_monotype_sized tl_monotype_arrow_get_args(tl_monotype *self) {
    tl_monotype *tup = tl_monotype_arrow_args(self);
    assert(tl_monotype_is_tuple(tup));
    return tup->list.xs;
}

void tl_monotype_sort_fvs(tl_monotype *self) {
    if (tl_arrow != self->tag) return;
    if (!self->list.fvs.size) return;
    qsort(self->list.fvs.v, self->list.fvs.size, sizeof self->list.fvs.v[0], str_cmp_v);
}

str_sized tl_monotype_fvs(tl_monotype *self) {
    if (tl_arrow != self->tag) return (str_sized){0};
    return self->list.fvs;
}

void tl_monotype_absorb_fvs(tl_monotype *self, str_sized fvs) {
    if (tl_arrow != self->tag) fatal("logic error");
    self->list.fvs = fvs;
}

u64 tl_type_constructor_def_hash64(tl_type_constructor_def *self) {
    assert(!str_is_empty(self->generic_name));
    u64 hash = str_hash64(self->generic_name);
    return hash;
}

u64 tl_monotype_sized_hash64_(u64, tl_monotype_sized, hashmap **, hashmap **);

u64 tl_monotype_hash64_(tl_monotype *self, hashmap **seen, hashmap **in_progress) {
    if (!self) return 0;

    u64 *found = map_get(*seen, &self, sizeof(tl_monotype *));
    if (found) {
        return *found;
    }

    u64 hash = hash64(&self->tag, sizeof self->tag);

    switch (self->tag) {
    case tl_placeholder: hash = str_hash64_combine(hash, self->placeholder); break;

    case tl_any:
    case tl_ellipsis:    break;
    case tl_var:
    case tl_weak:        hash = hash64_combine(hash, &self->var, sizeof self->var); break;

    case tl_integer:     hash = hash64_combine(hash, &self->integer, sizeof self->integer); break;

    case tl_cons_inst:   {
        tl_type_constructor_def *def      = self->cons_inst->def;
        u64                      def_hash = tl_type_constructor_def_hash64(def);
        hash                              = hash64_combine(hash, &def_hash, sizeof def_hash);

        // Check if self is an ancestor in progress
        u64 *ancestor = null;
        ancestor      = map_get(*in_progress, &def_hash, sizeof def_hash);
        if (ancestor) {
            // back-reference: use the def hash as a stable hash
            hash = hash64_combine(hash, ancestor, sizeof *ancestor);
        } else {

            // Recursive types: mark this in-progress
            map_set(in_progress, &def_hash, sizeof def_hash, &def_hash);

            tl_monotype_sized args = self->cons_inst->args;
            forall(i, args) {
                tl_monotype *arg = args.v[i];

                // Look through unary (e.g. Ptr) specializations to the target. If the target is the same
                // generic type constructor as ourselves, we simply tag it as "Self" and go no further.
                if (tl_monotype_is_unary(arg)) {
                    tl_monotype *target = tl_monotype_unary_target(arg);

                    // Check if target is an ancestor in progress
                    u64 *ancestor = null;
                    if (tl_monotype_is_inst(target)) {
                        u64 ancestor_hash = tl_type_constructor_def_hash64(target->cons_inst->def);
                        ancestor          = map_get(*in_progress, &ancestor_hash, sizeof ancestor_hash);
                    }
                    if (ancestor) {
                        // back-reference: use the def hash as a stable hash
                        hash = hash64_combine(hash, ancestor, sizeof *ancestor);
                    } else {
                        hash  = str_hash64_combine(hash, S("Unary"));
                        u64 h = tl_monotype_hash64_(target, seen, in_progress);
                        hash  = hash64_combine(hash, &h, sizeof h);
                    }

                } else {
                    u64 h = tl_monotype_hash64_(arg, seen, in_progress);
                    hash  = hash64_combine(hash, &h, sizeof h);
                }
            }

            // Remove from in-progress
            map_erase(*in_progress, &def_hash, sizeof def_hash);
        }

        // important: do not include special_name as part of hash, because specialize_user_type uses
        // unspecialised name + hash to de-duplicate

    } break;

    case tl_arrow:
    case tl_tuple: {
        hash = tl_monotype_sized_hash64_(hash, self->list.xs, seen, in_progress);
        if (tl_arrow == self->tag) hash = str_array_hash64(hash, self->list.fvs);
    } break;

    case tl_literal: {
        hash  = str_hash64_combine(hash, S("Literal"));
        u64 h = tl_monotype_hash64_(self->literal, seen, in_progress);
        hash  = hash64_combine(hash, &h, sizeof h);
    } break;
    }

    map_set(seen, &self, sizeof(tl_monotype *), &hash); // memoise
    return hash;
}

u64 tl_monotype_hash64(tl_monotype *self) {

    hashmap *seen        = map_new(transient_allocator, tl_monotype *, u64, 8);
    hashmap *in_progress = map_new(transient_allocator, u64, u64, 8);
    u64      out         = tl_monotype_hash64_(self, &seen, &in_progress);
    return out;
}

str tl_monotype_to_string_(allocator *alloc, tl_monotype *self, hashmap **map) {
    if (!self) return S("[null]");
    str *found = map_get(*map, &self, sizeof(void *));
    if (found) return *found;
    str provisional = str_init(alloc, "[recur]");
    map_set(map, &self, sizeof(void *), &provisional);

    str_build b = str_build_init(alloc, 64);

    switch (self->tag) {
    case tl_placeholder:
        //
        str_build_cat(&b, S("PLACEHOLDER"));
        break;

    case tl_any:
        //
        str_build_cat(&b, S("any"));
        break;

    case tl_ellipsis:
        //
        str_build_cat(&b, S("..."));
        break;

    case tl_integer: {
        char buf[64];
        snprintf(buf, sizeof buf, "i%i", self->integer);
        str_build_cat(&b, str_init(alloc, buf));
    } break;

    case tl_var: {
        char buf[64];
        snprintf(buf, sizeof buf, "t%u", self->var);
        str_build_cat(&b, str_init(alloc, buf));
    } break;

    case tl_weak: {
        char buf[64];
        snprintf(buf, sizeof buf, "w%u", self->var);
        str_build_cat(&b, str_init(alloc, buf));
    } break;

    case tl_cons_inst: {
        if (!str_is_empty(self->cons_inst->special_name)) str_build_cat(&b, self->cons_inst->special_name);
        else str_build_cat(&b, self->cons_inst->def->name);
        if (self->cons_inst->args.size) {
            str_build_cat(&b, S("("));
            forall(i, self->cons_inst->args) {
                str_build_cat(&b, tl_monotype_to_string_(alloc, self->cons_inst->args.v[i], map));
                if (i + 1 < self->cons_inst->args.size) str_build_cat(&b, S(", "));
            }
            str_build_cat(&b, S(")"));
        }
    } break;

    case tl_arrow: {
        if (self->list.fvs.size) {
            str_build_cat(&b, S("["));
            forall(i, self->list.fvs) {
                str_build_cat(&b, self->list.fvs.v[i]);
                if (i + 1 < self->list.fvs.size) str_build_cat(&b, S(" "));
            }
            str_build_cat(&b, S("] "));
        }
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string_(alloc, self->list.xs.v[i], map));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(" -> "));
        }
        str_build_cat(&b, S(")"));
    } break;

    case tl_tuple: {
        str_build_cat(&b, S("("));
        forall(i, self->list.xs) {
            str_build_cat(&b, tl_monotype_to_string_(alloc, self->list.xs.v[i], map));
            if (i + 1 < self->list.xs.size) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S(")"));

    } break;

    case tl_literal: {
        str_build_cat(&b, S("(literal "));
        str_build_cat(&b, tl_monotype_to_string_(alloc, self->literal, map));
        str_build_cat(&b, S(")"));
    } break;
    }

    str out = str_build_finish(&b);
    map_set(map, &self, sizeof(void *), &out);
    return out;
}

str tl_monotype_to_string(allocator *alloc, tl_monotype *self) {
    hashmap *map = map_create(transient_allocator, sizeof(str), 32);
    str      out = tl_monotype_to_string_(alloc, self, &map);
    return out;
}

str tl_polytype_to_string(allocator *alloc, tl_polytype *self) {
    str_build b = str_build_init(alloc, 64);

    if (self->quantifiers.size) {
        str_build_cat(&b, S("forall"));
        forall(i, self->quantifiers) {
            char buf[64];
            snprintf(buf, sizeof buf, "t%u", self->quantifiers.v[i]);
            str_build_cat(&b, S(" "));
            str_build_cat(&b, str_init(alloc, buf));
        }
        str_build_cat(&b, S(". "));
    }

    str_build_cat(&b, tl_monotype_to_string(alloc, self->type));

    return str_build_finish(&b);
}

tl_monotype *tl_monotype_arrow_result(tl_monotype *self) {
    if (!tl_monotype_is_arrow(self)) fatal("logic error");
    if (2 != self->list.xs.size) fatal("runtime error");
    return self->list.xs.v[1];
}

// -- substitutions --

tl_type_subs *tl_type_subs_create(allocator *alloc) {
    tl_type_subs *self = new (alloc, tl_type_subs);
    *self              = (tl_type_subs){
                   .data = (tl_type_uf_node_array){.alloc = alloc},
    };
    array_reserve(self->data, 1024);
    return self;
}

void tl_type_subs_destroy(allocator *alloc, tl_type_subs **p) {
    if (!p || !*p) return;
    array_free((*p)->data);
    alloc_free(alloc, *p);
    *p = null;
}

tl_type_variable tl_type_subs_fresh(tl_type_subs *self) {
    tl_type_uf_node x = {.parent = self->data.size};
    array_push(self->data, x);
    return x.parent;
}

static tl_type_variable uf_find(tl_type_subs *self, tl_type_variable tv) {
    assert(tv < self->data.size);
    if (self->data.v[tv].parent != tv) {
        // path compression
        self->data.v[tv].parent = uf_find(self, self->data.v[tv].parent);
    }
    return self->data.v[tv].parent;
}

static void uf_union(tl_type_subs *self, tl_type_variable tv1, tl_type_variable tv2) {
    tl_type_variable x = uf_find(self, tv1);
    tl_type_variable y = uf_find(self, tv2);
    assert(max(x, y) < self->data.size);

    if (x == y) return;

    // merge by rank
    if (self->data.v[x].rank < self->data.v[y].rank) swap(x, y);

    // make x the new root, with rank >= y rank
    self->data.v[y].parent = x;
    if (self->data.v[x].rank == self->data.v[y].rank) self->data.v[x].rank++;
}

static int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
                      tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen);
static int unify_tuple(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right,
                       tl_monotype *lhs, tl_monotype *rhs, type_error_cb_fun cb, void *user,
                       hashmap **seen);
static int tl_type_subs_unity_tv_tv(tl_type_subs *, tl_type_variable, tl_type_variable, type_error_cb_fun,
                                    void *, hashmap **seen);
static int tl_type_subs_unify_tv_weak(tl_type_subs *, tl_type_variable, tl_monotype *, type_error_cb_fun,
                                      void *, hashmap **seen);
static int tl_type_subs_unify_weak(tl_type_subs *, tl_monotype *weak, tl_monotype *, type_error_cb_fun,
                                   void *, hashmap            **seen);
int        tl_type_subs_unify_tv_mono(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                                      type_error_cb_fun cb, void *user, hashmap **seen);
int tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                            void *user, hashmap **);

static int unify_type_constructor_def(tl_type_constructor_def *lhs, tl_type_constructor_def *rhs) {
    if (lhs == rhs) return 0;
    if (str_eq(lhs->name, rhs->name)) return 0;
    if (str_eq(lhs->generic_name, rhs->generic_name)) return 0;

    return 1;
}

int unify_type_constructor_union(tl_type_subs *subs, tl_monotype *left, tl_monotype *right,
                                 type_error_cb_fun cb, void *user, hashmap **seen) {
    assert(tl_monotype_is_inst(left));
    assert(left->cons_inst->def->is_variable_args);

    tl_monotype_sized unions = left->cons_inst->args;

    switch (right->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_literal:     return 1;

    case tl_any:
    case tl_ellipsis:    return 0;

    case tl_var:         return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen);
    case tl_weak:        return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);

    case tl_cons_inst:   {
        if (right->cons_inst->def->is_variable_args) {
            tl_monotype_sized right_unions = right->cons_inst->args;
            forall(i, unions) {
                forall(j, right_unions) {
                    // don't pass cb so that any error is a soft error
                    if (0 ==
                        tl_type_subs_unify_mono(subs, unions.v[i], right_unions.v[j], null, null, seen))
                        return 0;
                }
            }
        } else {
            forall(i, unions) {
                // don't pass cb so that any error is a soft error
                if (0 == tl_type_subs_unify_mono(subs, unions.v[i], right, null, null, seen)) return 0;
            }
        }
    } break;

    case tl_arrow:
    case tl_tuple:
        // attempt to union with any of the union types
        forall(i, unions) {
            if (0 == tl_type_subs_unify_mono(subs, unions.v[i], right, cb, user, seen)) return 0;
        }
        break;
    }

    if (cb) cb(user, left, right);
    return 1;
}

int unify_type_constructor(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                           void *user, hashmap **seen) {
    assert(tl_monotype_is_inst(left));
    if (left->cons_inst->def->is_variable_args)
        return unify_type_constructor_union(subs, left, right, cb, user, seen);

    switch (right->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_literal:     return 1;

    case tl_any:
    case tl_ellipsis:    return 0;

    case tl_var:         return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen);
    case tl_weak:        return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);

    case tl_cons_inst:   {

        if (right->cons_inst->def->is_variable_args)
            return unify_type_constructor_union(subs, right, left, cb, user, seen);

        if (unify_type_constructor_def(left->cons_inst->def, right->cons_inst->def)) {
            if (cb) cb(user, left, right);
            return 1;
        }
        return unify_list(subs, left->cons_inst->args, right->cons_inst->args, left, right, cb, user, seen);
    }
    case tl_arrow:
    case tl_tuple:
        if (cb) cb(user, left, right);
        return 1;
    }
    fatal("unreachable");
}

int unify_type_literal(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                       void *user, hashmap **seen) {
    assert(tl_monotype_is_type_literal(left));

    // A type literal starts life with a type variable target. It will unify with any other literal by
    // unifying its targets. It will also unify with type variables. It will not unify with any other type.
    // In particular, note that it does NOT unify with any or ellipsis.

    switch (right->tag) {
    case tl_literal:     return tl_type_subs_unify_mono(subs, left->literal, right->literal, cb, user, seen);

    case tl_var:         return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen);
    case tl_weak:        return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);

    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:       return 1;
    }
    return 1;
}

int tl_type_subs_unify_mono(tl_type_subs *subs, tl_monotype *left, tl_monotype *right, type_error_cb_fun cb,
                            void *user, hashmap **seen) {
    if (!left || !right) return 1;

    tl_monotype_pair pair = {.left = left, .right = right};
    if (hset_contains(*seen, &pair, sizeof(pair))) return 0;
    swap(pair.left, pair.right);
    if (hset_contains(*seen, &pair, sizeof(pair))) return 0;
    hset_insert(seen, &pair, sizeof(pair));

    // `any` types unify with everything but are not concrete, so they don't resolve type variables
    if (tl_monotype_is_any(left) || tl_monotype_is_any(right)) return 0;

    // `ellipsis` types unify with everything but are not concrete. In addition, when part of a tuple, they
    // act as if the correct number of `any` types are present as required to unify with the target tuple.
    if (tl_monotype_is_ellipsis(left) || tl_monotype_is_ellipsis(right)) return 0;

    // integer-convertible types always unify
    if (tl_monotype_is_integer_convertible(left) && tl_monotype_is_integer_convertible(right)) return 0;

    // float-convertible types always unify
    if (tl_monotype_is_float_convertible(left) && tl_monotype_is_float_convertible(right)) return 0;

    if (tl_monotype_is_type_literal(left)) return unify_type_literal(subs, left, right, cb, user, seen);
    if (tl_monotype_is_type_literal(right)) return unify_type_literal(subs, right, left, cb, user, seen);
    if (tl_monotype_is_inst(left)) return unify_type_constructor(subs, left, right, cb, user, seen);
    if (tl_monotype_is_inst(right)) return unify_type_constructor(subs, right, left, cb, user, seen);

    switch (left->tag) {
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:
    case tl_literal:     fatal("unreachable");

    case tl_integer:     return !(tl_integer == right->tag && left->integer == right->integer);

    case tl_var:
        switch (right->tag) {
        case tl_integer:     return 1;

        case tl_placeholder:
        case tl_any:
        case tl_ellipsis:
        case tl_literal:     fatal("unreachable");

        case tl_var:         return tl_type_subs_unity_tv_tv(subs, left->var, right->var, cb, user, seen);
        case tl_weak:        return tl_type_subs_unify_tv_weak(subs, left->var, right, cb, user, seen);

        case tl_cons_inst:

        case tl_arrow:
        case tl_tuple:       return tl_type_subs_unify_tv_mono(subs, left->var, right, cb, user, seen);
        }
        break;

    case tl_weak:
        switch (right->tag) {
        case tl_integer:     return 1;

        case tl_placeholder:
        case tl_any:
        case tl_ellipsis:
        case tl_literal:     fatal("unreachable");

        case tl_var:         return tl_type_subs_unify_tv_weak(subs, right->var, left, cb, user, seen);

        case tl_weak:
            // unify two weak variables: put them in same equivalence class
            return tl_type_subs_unity_tv_tv(subs, left->var, right->var, cb, user, seen);

        case tl_cons_inst:
        case tl_arrow:
        case tl_tuple:     return tl_type_subs_unify_weak(subs, left, right, cb, user, seen);
        }
        break;

    case tl_cons_inst: fatal("unreachable"); break;

    case tl_arrow:
        switch (right->tag) {
        case tl_integer:     return 1;

        case tl_placeholder:
        case tl_any:
        case tl_ellipsis:
        case tl_literal:     fatal("unreachable");

        case tl_var:         return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen);
        case tl_weak:        return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);

        case tl_cons_inst:
        case tl_tuple:
            if (cb) cb(user, left, right);
            return 1;

        case tl_arrow: return unify_list(subs, left->list.xs, right->list.xs, left, right, cb, user, seen);
        }

        break;

    case tl_tuple:
        switch (right->tag) {
        case tl_integer:     return 1;

        case tl_placeholder:
        case tl_any:
        case tl_ellipsis:
        case tl_literal:     fatal("unreachable");

        case tl_var:         return tl_type_subs_unify_tv_mono(subs, right->var, left, cb, user, seen);
        case tl_weak:        return tl_type_subs_unify_weak(subs, right, left, cb, user, seen);

        case tl_cons_inst:
        case tl_arrow:
            if (cb) cb(user, left, right);
            return 1;

        case tl_tuple: return unify_tuple(subs, left->list.xs, right->list.xs, left, right, cb, user, seen);
        }

        break;
    }
    fatal("unreachable");
}

int unify_list(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
               tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen) {
    if (left.size != right.size) {
        if (cb) cb(user, lhs, rhs);
        return 1;
    }

    forall(i, left) {
        if (tl_type_subs_unify_mono(subs, left.v[i], right.v[i], cb, user, seen)) {
            if (cb) cb(user, lhs, rhs);
            return 1;
        }
    }

    return 0;
}

int unify_tuple(tl_type_subs *subs, tl_monotype_sized left, tl_monotype_sized right, tl_monotype *lhs,
                tl_monotype *rhs, type_error_cb_fun cb, void *user, hashmap **seen) {
    // Care must be taken when unifying tuples, because an ellipsis type will automatically unify with any
    // number of elements.

    forall(i, left) {
        if (tl_monotype_is_ellipsis(left.v[i])) goto success;
        if (i + 1 < right.size && tl_monotype_is_ellipsis(right.v[i])) goto success;
        if (i >= right.size || tl_type_subs_unify_mono(subs, left.v[i], right.v[i], cb, user, seen)) {
            if (cb) cb(user, lhs, rhs);
            return 1;
        }
    }

    if (left.size != right.size) {
        if (cb) cb(user, lhs, rhs);
        return 1;
    }

    return 0;

success:
    return 0;
}

static int tl_type_subs_monotype_occurs_(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                                         hashmap **seen) {
    if (!mono) return 0;
    if (hset_contains(*seen, &mono, sizeof(void *))) return 0;
    hset_insert(seen, &mono, sizeof(void *));

    switch (mono->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    return 0;

    case tl_var:
    case tl_weak:        {
        tl_type_variable root = uf_find(self, mono->var);
        if (root == tv) {
            return 1;
        }
        tl_monotype *resolved = self->data.v[root].type;
        if (resolved) return tl_type_subs_monotype_occurs_(self, tv, resolved, seen);

    } break;

    case tl_literal:
        //
        return tl_type_subs_monotype_occurs_(self, tv, mono->literal, seen);

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == mono->tag) arr = mono->cons_inst->args;
        else arr = mono->list.xs;
        forall(i, arr) {
            // Allow Ptr(tv): this is explicit support for recursive types where a field with a Ptr type is
            // used to refer to itself.
            if (tl_monotype_is_ptr(arr.v[i])) {
                tl_monotype *target = tl_monotype_ptr_target(arr.v[i]);
                if (tl_monotype_is_tv(target) && target->var == tv) continue;
            }
            if (tl_type_subs_monotype_occurs_(self, tv, arr.v[i], seen)) {
                return 1;
            }
        }
    } break;
    }

    return 0;
}

int tl_type_subs_monotype_occurs(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono) {
    hashmap *seen = hset_create(transient_allocator, 32);
    int      res  = tl_type_subs_monotype_occurs_(self, tv, mono, &seen);
    return res;
}

static int tl_type_subs_unity_tv_tv(tl_type_subs *self, tl_type_variable left, tl_type_variable right,
                                    type_error_cb_fun cb, void *user, hashmap **seen) {
    if (left == right) return 0;

    tl_type_variable left_root  = uf_find(self, left);
    tl_type_variable right_root = uf_find(self, right);
    if (left_root == right_root) return 0; // already in same equivalence class

    tl_monotype *left_type  = self->data.v[left_root].type;
    tl_monotype *right_type = self->data.v[right_root].type;
    if (left_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user, seen)) {
            return 1;
        }
    }

    // union the two classes
    uf_union(self, left_root, right_root);

    // preserve the resolved type, if any
    tl_type_variable union_root   = uf_find(self, left_root);
    self->data.v[union_root].type = left_type ? left_type : right_type;
    return 0;
}

static int tl_type_subs_unify_tv_weak(tl_type_subs *self, tl_type_variable left, tl_monotype *right,
                                      type_error_cb_fun cb, void *user, hashmap **seen) {
    if (tl_weak != right->tag) fatal("logic error");

    tl_type_variable left_root  = uf_find(self, left);

    tl_monotype     *left_type  = self->data.v[left_root].type;
    tl_monotype     *right_type = right;
    if (left_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, left_type, right_type, cb, user, seen)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->data.v[left_root].type = right;

    return 0;
}

static int tl_type_subs_unify_weak(tl_type_subs *self, tl_monotype *weak, tl_monotype *right,
                                   type_error_cb_fun cb, void *user, hashmap **seen) {
    if (tl_weak != weak->tag) fatal("logic error");

    tl_type_variable weak_root  = uf_find(self, weak->var);

    tl_monotype     *weak_type  = self->data.v[weak_root].type;
    tl_monotype     *right_type = right;
    if (weak_type && right_type) {
        // both are resolved: must unify
        if (tl_type_subs_unify_mono(self, weak_type, right_type, cb, user, seen)) {
            return 1;
        }
    }

    // store the weak type at the root
    self->data.v[weak_root].type = right;

    return 0;
}

int tl_type_subs_unify_tv_mono(tl_type_subs *self, tl_type_variable tv, tl_monotype *mono,
                               type_error_cb_fun cb, void *user, hashmap **seen) {
    if (tl_type_subs_monotype_occurs(self, tv, mono)) return 1;

    tl_type_variable tv_root = uf_find(self, tv);

    switch (mono->tag) {
    case tl_placeholder: return 1;

    case tl_any:
    case tl_ellipsis:
        // unifies with everything, but does not resolve
        return 0;

    case tl_var:
        // case 1: both are tvs
        return tl_type_subs_unity_tv_tv(self, tv, mono->var, cb, user, seen);
    case tl_weak:
        // case 2: one is weak type variable
        return tl_type_subs_unify_tv_weak(self, tv, mono, cb, user, seen);

    case tl_integer:
    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:
    case tl_literal:   {
        // case 3: tv = concrete type or arrow or tuple
        tl_monotype *tv_type = self->data.v[tv_root].type;
        if (tv_type) {
            // must unify
            return tl_type_subs_unify_mono(self, tv_type, mono, cb, user, seen);
        }

        // store the type at the root
        self->data.v[tv_root].type = mono;

    } break;
    }

    return 0;
}

static void tl_monotype_substitute_(allocator *alloc, tl_monotype *self, tl_type_subs *subs,
                                    hashmap *exclude, hashmap **seen) {
    // exclude may be null.
    if (!self) return;
    if (hset_contains(*seen, &self, sizeof(void *))) return;
    hset_insert(seen, &self, sizeof(void *));

    switch (self->tag) {
    case tl_integer:
    case tl_placeholder:
    case tl_any:
    case tl_ellipsis:    break;

    case tl_var:
    case tl_weak:        {

        if (exclude && hset_contains(exclude, &self->var, sizeof self->var)) return;
        tl_type_variable root = uf_find((tl_type_subs *)subs, self->var);
        if (exclude && hset_contains(exclude, &root, sizeof root)) return;

        tl_monotype *resolved = subs->data.v[root].type;
        if (resolved) {
            if (!tl_monotype_is_concrete_no_weak(resolved)) {
                tl_monotype_substitute_(alloc, resolved, subs, exclude, seen);
            }

            *self = *resolved;
            hset_insert(seen, &self, sizeof(void *));
        } else {
            // update to representative tv
            self->var = root;
        }

    } break;

    case tl_literal:
        //
        tl_monotype_substitute_(alloc, self->literal, subs, exclude, seen);
        break;

    case tl_cons_inst:
    case tl_arrow:
    case tl_tuple:     {
        tl_monotype_sized arr;
        if (tl_cons_inst == self->tag) arr = self->cons_inst->args;
        else arr = self->list.xs;
        forall(i, arr) {
            tl_monotype_substitute_(alloc, arr.v[i], subs, exclude, seen);
        }

    } break;
    }
}

void tl_monotype_substitute(allocator *alloc, tl_monotype *self, tl_type_subs *subs, hashmap *exclude) {
    hashmap *seen = hset_create(transient_allocator, 32);
    tl_monotype_substitute_(alloc, self, subs, exclude, &seen);
}

static void tl_polytype_substitute_ext(allocator *alloc, tl_polytype *self, tl_type_subs *subs,
                                       hashmap **exclude) {
    if (exclude) map_reset(*exclude);

    if (exclude && self->quantifiers.size) {
        forall(i, self->quantifiers) {
            hset_insert(exclude, &self->quantifiers.v[i], sizeof(tl_type_variable));
        }
    }

    tl_monotype_substitute(alloc, self->type, subs, exclude ? *exclude : null);
}

void tl_polytype_substitute(allocator *alloc, tl_polytype *self, tl_type_subs *subs) {
    hashmap *exclude = null;

    if (self->quantifiers.size) exclude = map_create(alloc, sizeof(tl_type_variable), 8);
    tl_polytype_substitute_ext(alloc, self, subs, exclude ? &exclude : null);
    if (exclude) map_destroy(&exclude);
}

tl_polytype tl_polytype_wrap(tl_monotype *mono) {
    return (tl_polytype){.type = mono};
}

void tl_type_subs_apply(tl_type_subs *subs, tl_type_env *env) {
    hashmap         *exclude = map_create(transient_allocator, sizeof(tl_type_variable), 8);

    hashmap_iterator iter    = {0};
    while (map_iter(env->map, &iter)) {
        tl_polytype *poly = *(tl_polytype **)iter.data;
        tl_polytype_substitute_ext(subs->data.alloc, poly, subs, &exclude);
    }
}

// --------------------------------------------------------------------------

str tl_type_subs_to_string(allocator *alloc, tl_type_subs *self) {
    return str_copy(alloc, S("not implemented"));
    (void)self;
}

// -- env --

void tl_type_env_log(tl_type_env *self) {
    str_array sorted = str_map_sorted_keys(transient_allocator, self->map);
    forall(i, sorted) {
        str          name     = sorted.v[i];
        tl_polytype *type     = str_map_get_ptr(self->map, name);
        str          type_str = tl_polytype_to_string(transient_allocator, type);

        fprintf(stderr, "%.*s : %.*s\n", str_ilen(name), str_buf(&name), str_ilen(type_str),
                str_buf(&type_str));
        str_deinit(transient_allocator, &type_str);
    }
    array_free(sorted);
}

//

void tl_type_subs_log(tl_type_subs *self) {
    hashmap               *seen        = hset_create(transient_allocator, 128);
    tl_type_variable_array equiv_class = {.alloc = transient_allocator};

    forall(i, self->data) {
        tl_type_variable root = uf_find(self, i);
        if (hset_contains(seen, &root, sizeof root)) continue;
        hset_insert(&seen, &root, sizeof root);

        equiv_class.size = 0;
        forall(j, self->data) {
            if (uf_find(self, j) == root) array_push(equiv_class, j);
        }

        fprintf(stderr, "  {");
        forall(j, equiv_class) {
            fprintf(stderr, "t%u", equiv_class.v[j]);
            if (j + 1 < equiv_class.size) fprintf(stderr, ", ");
        }
        fprintf(stderr, "}");

        tl_monotype *type = self->data.v[root].type;
        if (type) {
            fprintf(stderr, " = ");
            str s = tl_monotype_to_string(transient_allocator, type);
            fprintf(stderr, "%.*s", str_ilen(s), str_buf(&s));
        }

        fprintf(stderr, "\n");
    }
}

//

u64 tl_monotype_sized_hash64_(u64 seed, tl_monotype_sized arr, hashmap **seen, hashmap **in_progress) {
    u64 hash = seed;
    forall(i, arr) {
        u64 h = tl_monotype_hash64_(arr.v[i], seen, in_progress);
        hash  = hash64_combine(hash, &h, sizeof h);
    }
    return hash;
}

u64 tl_monotype_sized_hash64(u64 seed, tl_monotype_sized arr) {
    hashmap *seen        = map_new(transient_allocator, tl_monotype *, u64, 8);
    hashmap *in_progress = map_new(transient_allocator, u64, u64, 8);
    u64      out         = tl_monotype_sized_hash64_(seed, arr, &seen, &in_progress);
    return out;
}

tl_monotype_sized tl_monotype_sized_clone(allocator *alloc, tl_monotype_sized in, hashmap **mapping) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, in.size);
    forall(i, in) {
        tl_monotype *ty = tl_monotype_clone_(alloc, in.v[i], mapping);
        array_push(arr, ty);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

tl_polytype_sized tl_polytype_sized_clone(allocator *alloc, tl_polytype_sized polys) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_polytype *clone = tl_polytype_clone(alloc, polys.v[i]);
        array_push(arr, clone);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype *tl_monotype_sized_last(tl_monotype_sized arr) {
    if (!arr.size) return null;
    return arr.v[arr.size - 1];
}

tl_polytype_sized tl_monotype_sized_clone_poly(allocator *alloc, tl_monotype_sized monos) {
    tl_polytype_array arr = {.alloc = alloc};
    array_reserve(arr, monos.size);
    forall(i, monos) {
        tl_polytype *poly = tl_polytype_absorb_mono(alloc, tl_monotype_clone(alloc, monos.v[i]));
        array_push(arr, poly);
    }
    array_shrink(arr);
    return (tl_polytype_sized)sized_all(arr);
}

tl_monotype_sized tl_polytype_sized_concrete(allocator *alloc, tl_polytype_sized polys) {
    tl_monotype_array arr = {.alloc = alloc};
    array_reserve(arr, polys.size);
    forall(i, polys) {
        tl_monotype *mono = tl_polytype_concrete(polys.v[i]);
        array_push(arr, mono);
    }
    array_shrink(arr);
    return (tl_monotype_sized)sized_all(arr);
}

static void dbg(tl_type_env *self, char const *restrict fmt, ...) {
    if (!self->verbose) return;

    char buf[256];

    snprintf(buf, sizeof buf, "tl_type_env: %s\n", fmt);

    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, buf, args); // NOLINT
    va_end(args);
}
