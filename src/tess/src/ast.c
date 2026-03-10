#include "ast.h"
#include "alloc.h"
#include "ast_tags.h"
#include "hash.h"
#include "hashmap.h"
#include "parser.h"
#include "sexp.h"
#include "str.h"
#include "type.h"

#include <assert.h>
#include <stdio.h>

#define DEBUG_TYPE_SET 0

ast_node *ast_node_create(allocator *alloc, ast_tag tag) {
    // FIXME this should probably be called alloc, because it doesn't
    // init the node like other _create functions do.
    ast_node *self = alloc_calloc(alloc, 1, sizeof *self);

    self->file     = "";
    self->line     = -1;
    self->type     = null;
    self->error    = tl_err_ok;

    self->tag      = tag;

    return self;
}

ast_node *ast_node_create_bool(allocator *alloc, int val) {
    ast_node *out  = ast_node_create(alloc, ast_bool);
    out->bool_.val = val;
    return out;
}

ast_node *ast_node_create_i64(allocator *alloc, i64 x) {
    ast_node *self = ast_node_create(alloc, ast_i64);
    self->i64.val  = x;
    return self;
}
ast_node *ast_node_create_u64(allocator *alloc, u64 x) {
    ast_node *self = ast_node_create(alloc, ast_u64);
    self->u64.val  = x;
    return self;
}
ast_node *ast_node_create_i64_z(allocator *alloc, i64 x) {
    ast_node *self  = ast_node_create(alloc, ast_i64_z);
    self->i64_z.val = x;
    return self;
}
ast_node *ast_node_create_u64_zu(allocator *alloc, u64 x) {
    ast_node *self   = ast_node_create(alloc, ast_u64_zu);
    self->u64_zu.val = x;
    return self;
}
ast_node *ast_node_create_f64(allocator *alloc, f64 x) {
    ast_node *self = ast_node_create(alloc, ast_f64);
    self->f64.val  = x;
    return self;
}
ast_node *ast_node_create_nfa(allocator *alloc, ast_node *name, ast_node_sized type_args,
                              ast_node_sized args) {
    ast_node *self                                = ast_node_create(alloc, ast_named_function_application);
    self->named_application.name                  = name;
    self->named_application.n_type_arguments      = type_args.size;
    self->named_application.type_arguments        = type_args.v;
    self->named_application.n_arguments           = args.size;
    self->named_application.arguments             = args.v;
    self->named_application.is_specialized        = 0;
    self->named_application.is_type_constructor   = 0;
    self->named_application.is_function_reference = 0;
    return self;
}
ast_node *ast_node_create_nfa_tc(allocator *alloc, ast_node *name, ast_node_sized type_args,
                                 ast_node_sized args) {
    ast_node *self                              = ast_node_create_nfa(alloc, name, type_args, args);
    self->named_application.is_type_constructor = 1;
    return self;
}
ast_node *ast_node_create_lfa(allocator *alloc, ast_node *lambda, ast_node_sized args) {
    ast_node *self                          = ast_node_create(alloc, ast_lambda_function_application);
    self->lambda_application.lambda         = lambda;
    self->lambda_application.arguments      = args.v;
    self->lambda_application.n_arguments    = args.size;
    self->lambda_application.is_specialized = 0;
    return self;
}

ast_node *ast_node_create_attribute_set(allocator *alloc, ast_node_sized nodes) {
    ast_node *self            = ast_node_create(alloc, ast_attribute_set);
    self->attribute_set.n     = nodes.size;
    self->attribute_set.nodes = nodes.v;
    return self;
}

ast_node *ast_node_create_body(allocator *alloc, ast_node_sized body) {
    ast_node *self         = ast_node_create(alloc, ast_body);
    self->body.expressions = body;
    self->body.defers      = (ast_node_sized){0};
    return self;
}

ast_node *ast_node_create_case(allocator *alloc, ast_node *expr, ast_node_sized conds, ast_node_sized arms,
                               ast_node *bin_pred, ast_node *union_annotation, int is_union) {
    // Note: is_union may be 0, 1, or 2. See ast.h
    ast_node *self               = ast_node_create(alloc, ast_case);
    self->case_.expression       = expr;
    self->case_.conditions       = conds;
    self->case_.arms             = arms;
    self->case_.binary_predicate = bin_pred;         // may be null
    self->case_.union_annotation = union_annotation; // type annotation for tagged union
    self->case_.is_union         = is_union;
    return self;
}

ast_node *ast_node_create_nil(allocator *alloc) {
    ast_node *self = ast_node_create(alloc, ast_nil);
    return self;
}
ast_node *ast_node_create_void(allocator *alloc) {
    ast_node *self = ast_node_create(alloc, ast_void);
    return self;
}

ast_node *ast_node_create_if_then_else(allocator *alloc, ast_node *cond, ast_node *yes, ast_node *no) {
    ast_node *self               = ast_node_create(alloc, ast_if_then_else);
    self->if_then_else.condition = cond;
    self->if_then_else.yes       = yes;
    self->if_then_else.no        = no;
    return self;
}
ast_node *ast_node_create_unary_op(allocator *alloc, ast_node *op, ast_node *operand) {
    ast_node *self         = ast_node_create(alloc, ast_unary_op);
    self->unary_op.op      = op;
    self->unary_op.operand = operand;
    return self;
}
ast_node *ast_node_create_binary_op(allocator *alloc, ast_node *op, ast_node *left, ast_node *right) {
    ast_node *self        = ast_node_create(alloc, ast_binary_op);
    self->binary_op.op    = op;
    self->binary_op.left  = left;
    self->binary_op.right = right;
    return self;
}
ast_node *ast_node_create_assignment(allocator *alloc, ast_node *name, ast_node *value) {
    ast_node *self                 = ast_node_create(alloc, ast_assignment);
    self->assignment.name          = name;
    self->assignment.value         = value;
    self->assignment.op            = null;
    self->assignment.is_field_name = 0;
    return self;
}
ast_node *ast_node_create_reassignment(allocator *alloc, ast_node *name, ast_node *value) {
    ast_node *self                 = ast_node_create(alloc, ast_reassignment);
    self->assignment.name          = name;
    self->assignment.value         = value;
    self->assignment.op            = null;
    self->assignment.is_field_name = 0;
    return self;
}

ast_node *ast_node_create_reassignment_op(allocator *alloc, ast_node *name, ast_node *value, ast_node *op) {
    ast_node *self                 = ast_node_create(alloc, ast_reassignment_op);
    self->assignment.name          = name;
    self->assignment.value         = value;
    self->assignment.op            = op;
    self->assignment.is_field_name = 0;
    return self;
}

ast_node *ast_node_create_return(allocator *alloc, ast_node *value, int is_break) {
    ast_node *self                   = ast_node_create(alloc, ast_return);
    self->return_.value              = value;
    self->return_.is_break_statement = is_break;
    return self;
}
ast_node *ast_node_create_continue(allocator *alloc) {
    ast_node *self = ast_node_create(alloc, ast_continue);
    return self;
}
ast_node *ast_node_create_while(allocator *alloc, ast_node *cond, ast_node *update, ast_node *body) {
    ast_node *self         = ast_node_create(alloc, ast_while);
    self->while_.condition = cond;
    self->while_.update    = update;
    self->while_.body      = body;
    return self;
}
ast_node *ast_node_create_let_in(allocator *alloc, ast_node *name, ast_node *value, ast_node *body) {
    ast_node *self     = ast_node_create(alloc, ast_let_in);
    self->let_in.name  = name;
    self->let_in.value = value;
    self->let_in.body  = body;
    return self;
}
ast_node *ast_node_create_let(allocator *alloc, ast_node *name, ast_node_sized type_args,
                              ast_node_sized args, ast_node *body) {
    ast_node *self              = ast_node_create(alloc, ast_let);
    self->let.name              = name;
    self->let.n_type_parameters = type_args.size;
    self->let.type_parameters   = type_args.v;
    self->let.n_parameters      = args.size;
    self->let.parameters        = args.v;
    self->let.body              = body;
    self->let.is_specialized    = 0;
    return self;
}
ast_node *ast_node_create_tuple(allocator *alloc, ast_node_sized xs) {
    ast_node *self         = ast_node_create(alloc, ast_tuple);
    self->tuple.n_elements = xs.size;
    self->tuple.elements   = xs.v;
    return self;
}

ast_node *ast_node_create_try(allocator *alloc, ast_node *operand) {
    ast_node *self     = ast_node_create(alloc, ast_try);
    self->try_.operand = operand;
    return self;
}

ast_node *ast_node_create_type_alias(allocator *alloc, ast_node *name, ast_node *target) {
    ast_node *self          = ast_node_create(alloc, ast_type_alias);
    self->type_alias.name   = name;
    self->type_alias.target = target;
    return self;
}

ast_node *ast_node_create_type_predicate(allocator *alloc, ast_node *name, ast_node *annotation) {
    ast_node *self                = ast_node_create(alloc, ast_type_predicate);
    self->type_predicate.lhs      = name;
    self->type_predicate.rhs      = annotation;
    self->type_predicate.is_valid = 0;
    return self;
}

ast_node *ast_node_create_arrow(allocator *alloc, ast_node *left, ast_node *right,
                                ast_node_sized type_parameters) {
    ast_node *self                = ast_node_create(alloc, ast_arrow);
    self->arrow.left              = left;
    self->arrow.right             = right;
    self->arrow.n_type_parameters = type_parameters.size;
    self->arrow.type_parameters   = type_parameters.v;
    return self;
}

ast_node *ast_node_create_sym_c(allocator *alloc, char const *str) {
    ast_node *self                 = ast_node_create(alloc, ast_symbol);
    self->symbol.name              = str_init(alloc, str);
    self->symbol.original          = str_empty();
    self->symbol.annotation        = null;
    self->symbol.annotation_type   = null;
    self->symbol.is_module_mangled = 0;
    self->symbol.module            = str_empty();
    return self;
}

ast_node *ast_node_create_sym(allocator *alloc, str str) {
    ast_node *self                 = ast_node_create(alloc, ast_symbol);
    self->symbol.name              = str_copy(alloc, str);
    self->symbol.original          = str_empty();
    self->symbol.annotation        = null;
    self->symbol.annotation_type   = null;
    self->symbol.is_module_mangled = 0;
    self->symbol.module            = str_empty();
    return self;
}

void ast_node_move(ast_node *dst, ast_node *src) {
    alloc_copy(dst, src);
    alloc_zero(src); // valid nil node
}

nodiscard ast_node *ast_node_clone(allocator *alloc, ast_node const *orig) {

    if (null == orig) return null;

    ast_node *clone = ast_node_create(alloc, orig->tag);

    clone->file     = orig->file;
    clone->line     = orig->line;

    if (orig->type) {
        clone->type = tl_polytype_clone(alloc, orig->type);
    } else clone->type = null;

    // clone common array for some tags
    if (TL_AST_HAS_ARRAY(clone->tag)) {
        struct ast_array *vclone = ast_node_arr(clone), *vorig = ast_node_arr((ast_node *)orig);
        vclone->n     = vorig->n;
        vclone->nodes = alloc_malloc(alloc, vorig->n * sizeof vclone->nodes[0]);
        for (u32 i = 0; i < vclone->n; ++i) vclone->nodes[i] = ast_node_clone(alloc, vorig->nodes[i]);
    }

    // clone the rest of the fields
    switch (clone->tag) {
    case ast_attribute_set:
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_void:
    case ast_tuple:         break;

    case ast_bool:          clone->bool_.val = orig->bool_.val; break;
    case ast_i64:           clone->i64.val = orig->i64.val; break;
    case ast_u64:           clone->u64.val = orig->u64.val; break;
    case ast_i64_z:         clone->i64_z.val = orig->i64_z.val; break;
    case ast_u64_zu:        clone->u64_zu.val = orig->u64_zu.val; break;
    case ast_f64:           clone->f64.val = orig->f64.val; break;

    case ast_arrow:         {
        struct ast_arrow *vclone = ast_node_arrow(clone), *vorig = ast_node_arrow((ast_node *)orig);
        vclone->left              = ast_node_clone(alloc, vorig->left);
        vclone->right             = ast_node_clone(alloc, vorig->right);
        u32        argc           = vorig->n_type_parameters;
        ast_node **argv           = vorig->type_parameters;
        vclone->n_type_parameters = argc;
        vclone->type_parameters   = alloc_malloc(alloc, argc * sizeof(argv[0]));
        for (u32 i = 0; i < argc; i++) vclone->type_parameters[i] = ast_node_clone(alloc, argv[i]);
    } break;

    case ast_reassignment:
    case ast_reassignment_op:
    case ast_assignment:      {
        struct ast_assignment *vclone = ast_node_assignment(clone),
                              *vorig  = ast_node_assignment((ast_node *)orig);
        vclone->name                  = ast_node_clone(alloc, vorig->name);
        vclone->value                 = ast_node_clone(alloc, vorig->value);
        vclone->op                    = ast_node_clone(alloc, vorig->op);
        vclone->is_field_name         = vorig->is_field_name;
    } break;

    case ast_symbol:
    case ast_string:
    case ast_char:   {
        struct ast_symbol *vclone = ast_node_sym(clone), *vorig = ast_node_sym((ast_node *)orig);
        vclone->name            = str_copy(alloc, vorig->name);
        vclone->original        = str_copy(alloc, vorig->original);
        vclone->module          = str_copy(alloc, vorig->module);
        vclone->annotation      = ast_node_clone(alloc, vorig->annotation);
        vclone->annotation_type = null;
        if (vorig->annotation_type) {
            vclone->annotation_type = tl_polytype_clone(alloc, vorig->annotation_type);
        } else vclone->annotation_type = null;
        vclone->is_module_mangled = vorig->is_module_mangled;

        vclone->attributes        = ast_node_clone(alloc, vorig->attributes);
    } break;

    case ast_hash_command: {
        clone->hash_command.full = str_copy(alloc, orig->hash_command.full);
        str_array arr            = {.alloc = alloc};
        forall(i, orig->hash_command.words) {
            str _t = str_copy(alloc, orig->hash_command.words.v[i]);
            array_push(arr, _t);
        }
        array_shrink(arr);
        clone->hash_command.words      = (str_sized)sized_all(arr);
        clone->hash_command.is_c_block = orig->hash_command.is_c_block;
    } break;

    case ast_let_in: {
        struct ast_let_in *vclone = ast_node_let_in(clone), *vorig = ast_node_let_in((ast_node *)orig);
        vclone->name  = ast_node_clone(alloc, vorig->name);
        vclone->value = ast_node_clone(alloc, vorig->value);
        vclone->body  = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_let: {
        struct ast_let *vclone = ast_node_let(clone), *vorig = ast_node_let((ast_node *)orig);

        vclone->n_type_parameters = vorig->n_type_parameters;
        vclone->type_parameters =
          alloc_malloc(alloc, vclone->n_type_parameters * sizeof vclone->type_parameters[0]);
        for (u32 i = 0; i < vclone->n_type_parameters; ++i)
            vclone->type_parameters[i] = ast_node_clone(alloc, vorig->type_parameters[i]);

        vclone->name = ast_node_clone(alloc, vorig->name);
        vclone->body = ast_node_clone(alloc, vorig->body);
    } break;

    case ast_if_then_else: {
        struct ast_if_then_else *vclone = ast_node_ifthen(clone),
                                *vorig  = ast_node_ifthen((ast_node *)orig);
        vclone->condition               = ast_node_clone(alloc, vorig->condition);
        vclone->yes                     = ast_node_clone(alloc, vorig->yes);
        vclone->no                      = ast_node_clone(alloc, vorig->no);
    } break;

    case ast_lambda_function: {
        struct ast_lambda_function *vclone = ast_node_lf(clone), *vorig = ast_node_lf((ast_node *)orig);
        vclone->body       = ast_node_clone(alloc, vorig->body);
        vclone->attributes = ast_node_clone(alloc, vorig->attributes);
    } break;

    case ast_lambda_function_application: {
        struct ast_lambda_application *vclone = ast_node_lambda(clone),
                                      *vorig  = ast_node_lambda((ast_node *)orig);
        vclone->lambda                        = ast_node_clone(alloc, vorig->lambda);
        vclone->is_specialized                = vorig->is_specialized;
    } break;

    case ast_named_function_application: {
        struct ast_named_application *vclone = ast_node_named(clone),
                                     *vorig  = ast_node_named((ast_node *)orig);

        vclone->n_type_arguments             = vorig->n_type_arguments;
        vclone->type_arguments =
          alloc_malloc(alloc, vclone->n_type_arguments * sizeof vclone->type_arguments[0]);
        for (u32 i = 0; i < vclone->n_type_arguments; ++i)
            vclone->type_arguments[i] = ast_node_clone(alloc, vorig->type_arguments[i]);

        vclone->name                  = ast_node_clone(alloc, vorig->name);
        vclone->is_specialized        = vorig->is_specialized;
        vclone->is_type_constructor   = vorig->is_type_constructor;
        vclone->is_function_reference = vorig->is_function_reference;
    } break;

    case ast_try:        clone->try_.operand = ast_node_clone(alloc, orig->try_.operand); break;

    case ast_type_alias: {
        clone->type_alias.name   = ast_node_clone(alloc, orig->type_alias.name);
        clone->type_alias.target = ast_node_clone(alloc, orig->type_alias.target);
    } break;

    case ast_type_predicate: {
        clone->type_predicate.lhs      = ast_node_clone(alloc, orig->type_predicate.lhs);
        clone->type_predicate.rhs      = ast_node_clone(alloc, orig->type_predicate.rhs);
        clone->type_predicate.is_valid = orig->type_predicate.is_valid;
    } break;

    case ast_return:
        //
        clone->return_.value              = ast_node_clone(alloc, orig->return_.value);
        clone->return_.is_break_statement = orig->return_.is_break_statement;
        break;

    case ast_while:
        clone->while_.condition = ast_node_clone(alloc, orig->while_.condition);
        clone->while_.update    = ast_node_clone(alloc, orig->while_.update);
        clone->while_.body      = ast_node_clone(alloc, orig->while_.body);
        break;

    case ast_trait_definition: {
        struct ast_trait_def *vclone = &clone->trait_def, *vorig = &((ast_node *)orig)->trait_def;

        vclone->name             = ast_node_clone(alloc, vorig->name);
        vclone->n_type_arguments = vorig->n_type_arguments;
        vclone->n_signatures     = vorig->n_signatures;
        vclone->n_parents        = vorig->n_parents;

        vclone->type_arguments   = alloc_malloc(alloc, vclone->n_type_arguments * sizeof(ast_node *));
        for (u32 i = 0; i < vclone->n_type_arguments; ++i)
            vclone->type_arguments[i] = ast_node_clone(alloc, vorig->type_arguments[i]);

        vclone->signatures = alloc_malloc(alloc, vclone->n_signatures * sizeof(ast_node *));
        for (u32 i = 0; i < vclone->n_signatures; ++i)
            vclone->signatures[i] = ast_node_clone(alloc, vorig->signatures[i]);

        vclone->parents = alloc_malloc(alloc, vclone->n_parents * sizeof(ast_node *));
        for (u32 i = 0; i < vclone->n_parents; ++i)
            vclone->parents[i] = ast_node_clone(alloc, vorig->parents[i]);

    } break;

    case ast_user_type_definition: {
        struct ast_user_type_def *vclone = ast_node_utd(clone), *vorig = ast_node_utd((ast_node *)orig);

        vclone->name              = ast_node_clone(alloc, vorig->name);
        vclone->n_fields          = vorig->n_fields;
        vclone->n_type_arguments  = vorig->n_type_arguments;
        vclone->is_union          = vorig->is_union;
        vclone->tagged_union_name = str_copy(alloc, vorig->tagged_union_name);

        vclone->field_names       = alloc_malloc(alloc, vclone->n_fields * sizeof(ast_node *));

        // enums have no field annotations
        if (vorig->field_annotations)
            vclone->field_annotations = alloc_malloc(alloc, vclone->n_fields * sizeof(ast_node *));
        else vclone->field_annotations = null;

        for (u32 i = 0; i < vclone->n_fields; ++i)
            vclone->field_names[i] = ast_node_clone(alloc, vorig->field_names[i]);

        if (vorig->field_annotations)
            for (u32 i = 0; i < vclone->n_fields; ++i)
                vclone->field_annotations[i] = ast_node_clone(alloc, vorig->field_annotations[i]);

        // Clone type_arguments (populated by parser for generic types)
        if (vorig->type_arguments) {
            vclone->type_arguments = alloc_malloc(alloc, vclone->n_type_arguments * sizeof(ast_node *));
            for (u32 i = 0; i < vclone->n_type_arguments; ++i)
                vclone->type_arguments[i] = ast_node_clone(alloc, vorig->type_arguments[i]);
        } else vclone->type_arguments = null;

        // field_types is computed by type inference, not parser
        vclone->field_types = null;

    } break;

    case ast_binary_op: {
        clone->binary_op.left  = ast_node_clone(alloc, orig->binary_op.left);
        clone->binary_op.op    = ast_node_clone(alloc, orig->binary_op.op);
        clone->binary_op.right = ast_node_clone(alloc, orig->binary_op.right);
    } break;

    case ast_body: {
        clone->body.expressions.v = alloc_malloc(alloc, orig->body.expressions.size * sizeof(ast_node *));
        clone->body.expressions.size = orig->body.expressions.size;
        forall(i, clone->body.expressions) {
            clone->body.expressions.v[i] = ast_node_clone(alloc, orig->body.expressions.v[i]);
        }

        clone->body.defers.v    = alloc_malloc(alloc, orig->body.defers.size * sizeof(ast_node *));
        clone->body.defers.size = orig->body.defers.size;
        forall(i, clone->body.defers) {
            clone->body.defers.v[i] = ast_node_clone(alloc, orig->body.defers.v[i]);
        }
    } break;

    case ast_case: {
        clone->case_.expression       = ast_node_clone(alloc, orig->case_.expression);
        clone->case_.binary_predicate = ast_node_clone(alloc, orig->case_.binary_predicate);
        clone->case_.union_annotation = ast_node_clone(alloc, orig->case_.union_annotation);
        clone->case_.conditions.v = alloc_malloc(alloc, orig->case_.conditions.size * sizeof(ast_node *));
        clone->case_.conditions.size = orig->case_.conditions.size;
        clone->case_.arms.v          = alloc_malloc(alloc, orig->case_.arms.size * sizeof(ast_node *));
        clone->case_.arms.size       = orig->case_.conditions.size;
        forall(i, clone->case_.conditions) {
            clone->case_.conditions.v[i] = ast_node_clone(alloc, orig->case_.conditions.v[i]);
        }
        forall(i, clone->case_.arms) {
            clone->case_.arms.v[i] = ast_node_clone(alloc, orig->case_.arms.v[i]);
        }
        clone->case_.is_union         = orig->case_.is_union;
        clone->case_.union_annotation = ast_node_clone(alloc, orig->case_.union_annotation);
    } break;

    case ast_unary_op: {
        clone->unary_op.operand = ast_node_clone(alloc, orig->unary_op.operand);
        clone->unary_op.op      = ast_node_clone(alloc, orig->unary_op.op);
    } break;
    }

    return clone;
}

str ast_node_str(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    return node->symbol.name;
}

str ast_node_name_original(ast_node const *node) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    if (str_is_empty(node->symbol.original)) return node->symbol.name;
    else return node->symbol.original;
}

void ast_node_name_replace(ast_node *node, str replace) {
    if (ast_symbol != node->tag && ast_string != node->tag) fatal("expected symbol or string");
    // Important: don't replace original name more than once: original name is used for many things,
    // including c_ prefixed function names. Type inference is sometimes repetitive, and will try to
    // 'specialize' the name multiple times.
    if (str_is_empty(node->symbol.original)) node->symbol.original = node->symbol.name;
    node->symbol.name = replace;
}

void ast_node_rewrite_to_nfa(ast_node *node, ast_node *name, ast_node **args, u8 n_args) {
    tl_polytype *type                             = node->type;
    node->tag                                     = ast_named_function_application;
    node->named_application.name                  = name;
    node->named_application.arguments             = args;
    node->named_application.n_arguments           = n_args;
    node->named_application.type_arguments        = null;
    node->named_application.n_type_arguments      = 0;
    node->named_application.is_specialized        = 0;
    node->named_application.is_type_constructor   = 0;
    node->named_application.is_function_reference = 0;
    node->type                                    = type;
}

ast_node *ast_node_lvalue(ast_node *self) {
    if (ast_node_is_symbol(self)) return self;
    else if (ast_binary_op == self->tag) return ast_node_lvalue(self->binary_op.right);
    else if (ast_unary_op == self->tag) return ast_node_lvalue(self->unary_op.operand);
    else fatal("unreachable");
}

ast_node *ast_node_op_rightmost(ast_node *self) {
    if (ast_binary_op == self->tag) return ast_node_op_rightmost(self->binary_op.right);
    else if (ast_unary_op == self->tag) return ast_node_op_rightmost(self->unary_op.operand);
    else return self;
}

void ast_node_type_set(ast_node *self, tl_polytype *type) {
#if DEBUG_TYPE_SET
    if (self->type && tl_monotype_is_tv(self->type->type)) {
        str node_str = v2_ast_node_to_string(default_allocator(), self);
        str poly_str = tl_polytype_to_string(default_allocator(), self->type);
        fprintf(stderr, "warning: ast_node_type_set: abandon type variable '%s': '%s'\n",
                str_cstr(&poly_str), str_cstr(&node_str));

        str_deinit(default_allocator(), &poly_str);
        str_deinit(default_allocator(), &node_str);
    }
#endif
    self->type = type;
}

void ast_node_set_attributes(ast_node *self, ast_node *attribute_set) {
    if (ast_node_is_symbol(self)) self->symbol.attributes = attribute_set;
    else if (ast_node_is_lambda_function(self)) self->lambda_function.attributes = attribute_set;
    else fatal("runtime error");
}

//

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *));

sexp do_ast_node_to_sexp(allocator *alloc, ast_node const *node,
                         sexp (*symbol_fun)(allocator *, ast_node const *));

sexp symbol_node_to_sexp(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);
    return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                               sexp_init_sym(alloc, ast_node_str(node)));
}

sexp symbol_node_to_sexp_for_error(allocator *alloc, ast_node const *node) {
    assert(node->tag == ast_symbol);

    if (!str_is_empty(node->symbol.original))
        return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                                   sexp_init_sym(alloc, node->symbol.original));
    else
        return sexp_init_list_pair(alloc, sexp_init_sym_c(alloc, "symbol"),
                                   sexp_init_sym(alloc, node->symbol.name));
}

// -- pool operations --

void ast_node_each_node(void *ctx, ast_node_each_node_fun fun, ast_node *node) {
    if (!node) return;

    // process node types that have a common ast_array
    if (TL_AST_HAS_ARRAY(node->tag)) {
        struct ast_array *v = ast_node_arr(node);
        for (u32 i = 0; i < v->n; ++i) fun(ctx, v->nodes[i]);
    }

    // process node types that have additional or no-array links
    switch (node->tag) {
    case ast_attribute_set:
    case ast_continue:
    case ast_ellipsis:
    case ast_eof:
    case ast_nil:
    case ast_void:
    case ast_bool:
    case ast_symbol:
    case ast_hash_command:
    case ast_i64:
    case ast_u64:
    case ast_i64_z:
    case ast_u64_zu:
    case ast_f64:
    case ast_string:
    case ast_char:
    case ast_tuple:
        //
        return;

    case ast_arrow:
        fun(ctx, node->arrow.left);
        fun(ctx, node->arrow.right);
        break;

    case ast_assignment:
    case ast_reassignment:
    case ast_reassignment_op:
        fun(ctx, node->assignment.name);
        fun(ctx, node->assignment.value);
        fun(ctx, node->assignment.op);
        break;

    case ast_let_in:
        fun(ctx, node->let_in.name);
        fun(ctx, node->let_in.value);
        fun(ctx, node->let_in.body);
        break;

    case ast_let:
        //
        fun(ctx, node->let.name);
        fun(ctx, node->let.body);
        break;

    case ast_if_then_else:
        fun(ctx, node->if_then_else.condition);
        fun(ctx, node->if_then_else.yes);
        fun(ctx, node->if_then_else.no);
        break;

    case ast_lambda_function:
        //
        fun(ctx, node->lambda_function.body);
        fun(ctx, node->lambda_function.attributes);
        break;

    case ast_lambda_function_application:
        //
        fun(ctx, node->lambda_application.lambda);
        break;

    case ast_named_function_application:
        //
        fun(ctx, node->named_application.name);
        break;

    case ast_return:
        //
        fun(ctx, node->return_.value);
        break;

    case ast_try:
        //
        fun(ctx, node->try_.operand);
        break;

    case ast_while:
        //
        fun(ctx, node->while_.condition);
        fun(ctx, node->while_.update);
        fun(ctx, node->while_.body);
        break;

    case ast_trait_definition: {
        struct ast_trait_def *v = &node->trait_def;
        fun(ctx, v->name);
        for (u32 i = 0; i < v->n_type_arguments; ++i) fun(ctx, v->type_arguments[i]);
        for (u32 i = 0; i < v->n_signatures; ++i) fun(ctx, v->signatures[i]);
        for (u32 i = 0; i < v->n_parents; ++i) fun(ctx, v->parents[i]);
    } break;

    case ast_user_type_definition: {
        struct ast_user_type_def *v = ast_node_utd(node);

        fun(ctx, node->user_type_def.name);

        for (u32 i = 0; i < v->n_fields; ++i) {
            if (v->field_annotations) fun(ctx, v->field_annotations[i]);
            fun(ctx, v->field_names[i]);
        }
    } break;

    case ast_type_alias:
        //
        fun(ctx, node->type_alias.name);
        fun(ctx, node->type_alias.target);
        break;

    case ast_type_predicate:
        //
        fun(ctx, node->type_predicate.lhs);
        fun(ctx, node->type_predicate.rhs);
        break;

    case ast_body:
        //
        forall(i, node->body.expressions) fun(ctx, node->body.expressions.v[i]);
        forall(i, node->body.defers) fun(ctx, node->body.defers.v[i]);
        break;

    case ast_case:
        fun(ctx, node->case_.expression);
        if (node->case_.binary_predicate) fun(ctx, node->case_.binary_predicate);
        if (node->case_.union_annotation) fun(ctx, node->case_.union_annotation);
        forall(i, node->case_.conditions) fun(ctx, node->case_.conditions.v[i]);
        forall(i, node->case_.arms) fun(ctx, node->case_.arms.v[i]);
        break;

    case ast_binary_op:
        fun(ctx, node->binary_op.op);
        fun(ctx, node->binary_op.left);
        fun(ctx, node->binary_op.right);
        break;

    case ast_unary_op:
        fun(ctx, node->unary_op.op);
        fun(ctx, node->unary_op.operand);
        break;
    }
}

//

ast_arguments_iter ast_node_arguments_iter(ast_node *node) {
    assert(ast_node_is_let(node) || ast_node_is_let_in_lambda(node) || ast_node_is_lambda_function(node) ||
           ast_node_is_lambda_application(node) || ast_node_is_nfa(node) || ast_node_is_tuple(node));
    if (ast_node_is_let_in_lambda(node)) node = node->let_in.value;

    // These variants all share the same layout for parameters or arguments:
    // ast_let, ast_named_function_application, ast_lambda_application, ast_lambda_function
    return (ast_arguments_iter){
      .index = 0,
      .count = node->array.n ? (node->array.nodes[0]->tag == ast_nil ? 0 : node->array.n) : 0,
      .nodes = (ast_node_sized){.size = node->array.n, .v = node->array.nodes}};
}

ast_node *ast_arguments_next(ast_arguments_iter *iter) {
    if (iter->index >= iter->nodes.size) return null;
    ast_node *node = iter->nodes.v[iter->index++];
    return node;
}

ast_node *ast_node_body(ast_node *self) {
    if (ast_node_is_let(self)) return self->let.body;
    else if (ast_node_is_let_in_lambda(self)) return self->let_in.value->lambda_function.body;
    else return null;
}

//

struct dfs_ctx {
    void      *caller_ctx;
    ast_op_fun fun;
    hashmap   *visited;
};

struct dfs_map_ctx {
    void          *caller_ctx;
    ast_op_map_fun fun;
    hashmap       *visited;
};

void        ast_node_dfs(void *caller_ctx, ast_node *node, ast_op_fun fun);

static void dfs_one(void *ctx_, ast_node *node) {
    struct dfs_ctx *ctx = ctx_;

    if (!node) return;

    // exclude type defs from dfs
    if (ast_trait_definition == node->tag) return;
    if (ast_user_type_definition == node->tag) return;

    if (ctx->visited) {
        if (hset_contains(ctx->visited, &node, sizeof(ast_node *))) return;
        hset_insert(&ctx->visited, &node, sizeof(ast_node *));
    }

    ast_node_each_node(ctx, dfs_one, node);
    ctx->fun(ctx->caller_ctx, node);
}

void ast_node_dfs(void *caller_ctx, ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    if (!node) return;

    struct dfs_ctx ctx = {.caller_ctx = caller_ctx, .fun = fun, .visited = null};
    ast_node_each_node(&ctx, dfs_one, node);
    fun(caller_ctx, node);
}

void ast_node_dfs_safe_for_recur(allocator *alloc, void *caller_ctx, ast_node *node, ast_op_fun fun) {

    // Note: const dfs also uses this function.

    if (!node) return;

    struct dfs_ctx ctx = {
      .caller_ctx = caller_ctx,
      .fun        = fun,
      .visited    = hset_create(alloc, 128),
    };

    ast_node_each_node(&ctx, dfs_one, node);
    fun(caller_ctx, node);

    hset_destroy(&ctx.visited);
}

void ast_node_cdfs(void *ctx, ast_node const *start, ast_op_cfun fun) {
    ast_node_dfs(ctx, (ast_node *)start, (ast_op_fun)fun);
}

// -- utilities --

#ifndef MOS_TAG_STRING
#define MOS_TAG_STRING(name, str) [name] = str,
#endif

char const *ast_tag_to_string(ast_tag tag) {

    static char const *const strings1[] = {
      "ast_nil",
      "ast_void",
      "ast_hash_command",
      "ast_arrow",
      "ast_assignment",
      "ast_binary_op",
      "ast_body",
      "ast_bool",
      "ast_case",
      "ast_char",
      "ast_continue",
      "ast_ellipsis",
      "ast_eof",
      "ast_f64",
      "ast_i64",
      "ast_if_then_else",
      "ast_let_in",
      "ast_reassignment",
      "ast_reassignment_op",
      "ast_return",
      "ast_string",
      "ast_symbol",
      "ast_try",
      "ast_type_alias",
      "ast_type_predicate",
      "ast_u64",
      "ast_i64_z",
      "ast_u64_zu",
      "ast_trait_definition",
      "ast_unary_op",
      "ast_user_type_definition",
      "ast_while",
    };

    static char const *const strings2[] = {
      "ast_lambda_function",
      "ast_lambda_function_application",
      "ast_attribute_set",
      "ast_let",
      "ast_named_function_application",
      "ast_tuple",
    };

    if (!TL_AST_HAS_ARRAY(tag)) {
        assert(tag < sizeof(strings1));
        return strings1[tag];
    }

    tag = TL_AST_CLEAR_BITS(tag);
    assert(tag < sizeof(strings2));
    return strings2[tag];
}

str v2_ast_node_to_string(allocator *alloc, ast_node const *node) {
    char buf[64];

    if (!node) return str_init(alloc, "[null]");

    str ty_str = node->type ? tl_polytype_to_string(alloc, node->type) : str_empty();

    switch (node->tag) {
    case ast_attribute_set: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("[["));
        for (u32 i = 0; i < node->attribute_set.n; i++) {
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->attribute_set.nodes[i]));
            if (i + 1 < node->attribute_set.n) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S("]]"));
        return str_build_finish(&b);
    } break;

    case ast_hash_command: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("#"));
        str_build_join_sized(&b, S(" "), node->hash_command.words);
        return str_build_finish(&b);
    } break;

    case ast_body: {
        str_build b = str_build_init(alloc, 128);
        forall(i, node->body.expressions) {
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->body.expressions.v[i]));
            str_build_cat(&b, S(" "));
        }
        return str_build_finish(&b);
    } break;

    case ast_case: {
        if (node->case_.conditions.size != node->case_.arms.size) fatal("logic error");

        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("(case "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->case_.expression));

        if (node->case_.binary_predicate) {
            str_build_cat(&b, S("(binary_predicate "));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->case_.binary_predicate));
            str_build_cat(&b, S(") "));
        }

        str_build_cat(&b, S("("));
        forall(i, node->case_.conditions) {
            str_build_cat(&b, S("("));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->case_.conditions.v[i]));
            str_build_cat(&b, S(") "));

            str_build_cat(&b, v2_ast_node_to_string(alloc, node->case_.arms.v[i]));

            if (i + 1 < node->case_.conditions.size) str_build_cat(&b, S(" "));
        }
        str_build_cat(&b, S(")"));

        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    } break;

    case ast_binary_op: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("(binary_op "));
        str_build_cat(&b, node->binary_op.op->symbol.name);
        str_build_cat(&b, S(" "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->binary_op.left));
        str_build_cat(&b, S(" "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->binary_op.right));
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);

    } break;

    case ast_f64: snprintf(buf, sizeof buf, "%f", node->f64.val); return str_init(alloc, buf);
    case ast_i64:
        snprintf(buf, sizeof buf, "(%" PRIi64 " : %s)", node->i64.val, str_cstr(&ty_str));
        return str_init(alloc, buf);

    case ast_u64: snprintf(buf, sizeof buf, "%" PRIu64, node->u64.val); return str_init(alloc, buf);
    case ast_i64_z:
        snprintf(buf, sizeof buf, "(%" PRIi64 "z : %s)", node->i64_z.val, str_cstr(&ty_str));
        return str_init(alloc, buf);
    case ast_u64_zu:
        snprintf(buf, sizeof buf, "%" PRIu64 "zu", node->u64_zu.val);
        return str_init(alloc, buf);
    case ast_string:   return str_cat_3(alloc, S("\""), node->symbol.name, S("\""));
    case ast_char:     return str_cat_3(alloc, S("'"), node->symbol.name, S("'"));
    case ast_bool:     return node->bool_.val ? str_copy(alloc, S("true")) : str_copy(alloc, S("false"));
    case ast_nil:      return S("()");
    case ast_void:     return S("void");
    case ast_continue: return S("continue");

    case ast_return:
        //
        if (!node->return_.is_break_statement)
            return str_cat(alloc, S("return "), v2_ast_node_to_string(alloc, node->return_.value));
        else return str_cat(alloc, S("break "), v2_ast_node_to_string(alloc, node->return_.value));

    case ast_try: return str_cat(alloc, S("try "), v2_ast_node_to_string(alloc, node->try_.operand));

    case ast_while:
        //
        return str_cat_6(alloc, S("while ("), v2_ast_node_to_string(alloc, node->while_.condition),
                         S("), ("), v2_ast_node_to_string(alloc, node->while_.update), S(") "),
                         v2_ast_node_to_string(alloc, node->while_.body));

    case ast_symbol: {
        str_build b = str_build_init(alloc, 64);

        if (node->type) str_build_cat(&b, S("(")); // wrap in () if type exists

        str name     = ast_node_str(node);
        str original = ast_node_name_original(node);
        // out          = str_cat(alloc, out, str_is_empty(original) ? name : original);
        str_build_cat(&b, name);
        if (!str_is_empty(original)) {
            str_build_cat(&b, S(" ("));
            str_build_cat(&b, original);
            str_build_cat(&b, S(")"));
        }

        if (node->symbol.annotation_type) {
            str_build_cat(&b, S(" (ann:"));
            str_build_cat(&b, tl_polytype_to_string(alloc, node->symbol.annotation_type));
            str_build_cat(&b, S(")"));
        } else if (node->symbol.annotation) {
            str_build_cat(&b, S(" ("));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->symbol.annotation));
            str_build_cat(&b, S(")"));
        }
        if (node->type) {
            str_build_cat(&b, S(" : "));
            str_build_cat(&b, tl_polytype_to_string(alloc, node->type));
            str_build_cat(&b, S(")"));
        }
        return str_build_finish(&b);
    }

    case ast_let: {
        str out               = str_copy(alloc, S("let "));
        out                   = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let.name));

        ast_node_sized params = ast_node_sized_from_ast_array((ast_node *)node);
        forall(i, params) {
            if (ast_nil == params.v[i]->tag) {
                out = str_cat(alloc, out, S(" ()"));
                break;
            } else {
                out = str_cat_3(alloc, out, S(" "), params.v[i]->symbol.name);
            }
        }

        if (node->type) out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type));
        out = str_cat_3(alloc, out, S(" = "), v2_ast_node_to_string(alloc, node->let.body));
        return out;
    }

    case ast_let_in: {
        str out = str_copy(alloc, S("let "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.name));
        out     = str_cat(alloc, out, S(" = "));
        out     = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.value));
        out     = str_cat(alloc, out, S(" in "));
        if (node->let_in.body) out = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->let_in.body));
        return out;
    }

    case ast_named_function_application: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(apply "));
        str_build_cat(&b, node->named_application.name->symbol.name);
        for (u32 i = 0, n = node->named_application.n_arguments; i < n; ++i) {
            str_build_cat(&b, S(" "));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->named_application.arguments[i]));
        }
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_arrow: {
        u32        argc = node->arrow.n_type_parameters;
        ast_node **argv = node->arrow.type_parameters;
        str_build  b    = str_build_init(alloc, 64);
        if (argc) {
            str_build_cat(&b, S("["));
            for (u32 i = 0; i < argc; i++) {
                str_build_cat(&b, v2_ast_node_to_string(alloc, argv[i]));
                if (i + 1 < argc) str_build_cat(&b, S(", "));
            }
            str_build_cat(&b, S("]"));
        }

        str_build_cat(&b, v2_ast_node_to_string(alloc, node->arrow.left));
        str_build_cat(&b, S(" -> "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->arrow.right));
        return str_build_finish(&b);
    }

    case ast_lambda_function: {
        str out = str_copy(alloc, S("fun"));

        // here we want to include the nil value
        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            out = str_cat_3(alloc, out, S(" "),
                            v2_ast_node_to_string(alloc, node->lambda_function.parameters[i]));
        }
        out = str_cat(alloc, out, S(" -> "));
        out = str_cat(alloc, out, v2_ast_node_to_string(alloc, node->lambda_function.body));
        return out;
    }

    case ast_type_alias: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(alias "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_alias.name));
        str_build_cat(&b, S(" "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_alias.target));
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_type_predicate: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(predicate "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_predicate.lhs));
        str_build_cat(&b, S(" :: "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->type_predicate.rhs));
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_trait_definition: {
        str out = str_copy(alloc, S("[trait_definition: "));
        str_dcat(alloc, &out, node->trait_def.name->symbol.name);
        str_dcat(alloc, &out, S("]"));
        return out;
    }

    case ast_user_type_definition: {
        str out = str_copy(alloc, S("[user_type_definition: "));
        str_dcat(alloc, &out, node->user_type_def.name->symbol.name);
        str_dcat(alloc, &out, S("]"));
        return out;
    }

    case ast_ellipsis:     return str_copy(alloc, S("..."));

    case ast_if_then_else: {
        str_build b = str_build_init(alloc, 80);
        str_build_cat(&b, S("if "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.condition));
        str_build_cat(&b, S(" { "));
        str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.yes));
        str_build_cat(&b, S("}"));
        if (node->if_then_else.no) {
            str_build_cat(&b, S(" else { "));
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->if_then_else.no));
            str_build_cat(&b, S("}"));
        }
        return str_build_finish(&b);
    }

    case ast_unary_op:
        return str_cat_5(alloc, S("(unary_op "), v2_ast_node_to_string(alloc, node->unary_op.op), S(" "),
                         v2_ast_node_to_string(alloc, node->unary_op.operand), S(")"));

    case ast_assignment:
        return str_cat_3(alloc, v2_ast_node_to_string(alloc, node->assignment.name), S(" = "),
                         v2_ast_node_to_string(alloc, node->assignment.value));

    case ast_reassignment:
        return str_cat_3(alloc, v2_ast_node_to_string(alloc, node->assignment.name), S(" re= "),
                         v2_ast_node_to_string(alloc, node->assignment.value));

    case ast_reassignment_op:
        return str_cat_3(alloc, v2_ast_node_to_string(alloc, node->assignment.name),
                         v2_ast_node_to_string(alloc, node->assignment.op),
                         v2_ast_node_to_string(alloc, node->assignment.value));

    case ast_eof:
    case ast_tuple: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(tuple "));
        for (u32 i = 0, n = node->tuple.n_elements; i < n; ++i) {
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->tuple.elements[i]));
            if (i + 1 < n) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_lambda_function_application: return str_copy(alloc, S("[lambda function application]"));
    }

    fatal("unreachable");
}

str ast_node_to_short_string(allocator *alloc, ast_node const *node) {

    if (!node) return str_init(alloc, "[null]");

    switch (node->tag) {
    case ast_attribute_set: {
        str_build b = str_build_init(alloc, 128);
        str_build_cat(&b, S("[["));
        for (u32 i = 0; i < node->attribute_set.n; i++) {
            str_build_cat(&b, v2_ast_node_to_string(alloc, node->attribute_set.nodes[i]));
            if (i + 1 < node->attribute_set.n) str_build_cat(&b, S(", "));
        }
        str_build_cat(&b, S("]]"));
        return str_build_finish(&b);
    } break;

    case ast_binary_op:
    case ast_body:
    case ast_case:
    case ast_hash_command:
    case ast_f64:
    case ast_i64:
    case ast_u64:
    case ast_i64_z:
    case ast_u64_zu:
    case ast_string:
    case ast_char:
    case ast_bool:
    case ast_nil:
    case ast_void:
    case ast_continue:
    case ast_return:
    case ast_try:
    case ast_while:
    case ast_ellipsis:
    case ast_if_then_else:
    case ast_unary_op:
    case ast_assignment:
    case ast_reassignment:
    case ast_reassignment_op:
    case ast_eof:
    case ast_tuple:
    case ast_lambda_function_application: return str_init_static(ast_tag_to_string(node->tag));

    case ast_type_predicate:
    case ast_symbol:                      return v2_ast_node_to_string(alloc, node);

    case ast_let:                         {
        str out               = str_copy(alloc, S("let "));
        out                   = str_cat(alloc, out, ast_node_to_short_string(alloc, node->let.name));

        ast_node_sized params = ast_node_sized_from_ast_array((ast_node *)node);
        forall(i, params) {
            if (ast_nil == params.v[i]->tag) {
                out = str_cat(alloc, out, S(" ()"));
                break;
            } else {
                out = str_cat_3(alloc, out, S(" "), params.v[i]->symbol.name);
            }
        }

        if (node->type) out = str_cat_3(alloc, out, S(" : "), tl_polytype_to_string(alloc, node->type));
        return out;
    }

    case ast_let_in: {
        str out = str_copy(alloc, S("let "));
        out     = str_cat(alloc, out, ast_node_to_short_string(alloc, node->let_in.name));
        out     = str_cat(alloc, out, S(" = "));
        out     = str_cat(alloc, out, ast_node_to_short_string(alloc, node->let_in.value));
        out     = str_cat(alloc, out, S(" in "));
        if (node->let_in.body) out = str_cat(alloc, out, S("..."));
        return out;
    }

    case ast_named_function_application: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(apply "));
        str_build_cat(&b, node->named_application.name->symbol.name);
        for (u32 i = 0, n = node->named_application.n_arguments; i < n; ++i) {
            str_build_cat(&b, S(" "));
            str_build_cat(&b, ast_node_to_short_string(alloc, node->named_application.arguments[i]));
        }
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_arrow: {
        str out = str_cat_3(alloc, ast_node_to_short_string(alloc, node->arrow.left), S(" -> "),
                            ast_node_to_short_string(alloc, node->arrow.right));
        return out;
    }

    case ast_lambda_function: {
        str out = str_copy(alloc, S("fun"));

        // here we want to include the nil value
        for (u32 i = 0; i < node->lambda_function.n_parameters; ++i) {
            out = str_cat_3(alloc, out, S(" "),
                            ast_node_to_short_string(alloc, node->lambda_function.parameters[i]));
        }
        out = str_cat(alloc, out, S(" -> ..."));
        return out;
    }

    case ast_type_alias: {
        str_build b = str_build_init(alloc, 64);
        str_build_cat(&b, S("(alias "));
        str_build_cat(&b, ast_node_to_short_string(alloc, node->type_alias.name));
        str_build_cat(&b, S(" "));
        str_build_cat(&b, ast_node_to_short_string(alloc, node->type_alias.target));
        str_build_cat(&b, S(")"));
        return str_build_finish(&b);
    }

    case ast_trait_definition: {
        str out = str_copy(alloc, S("[trait_definition: "));
        str_dcat(alloc, &out, node->trait_def.name->symbol.name);
        str_dcat(alloc, &out, S("]"));
        return out;
    }

    case ast_user_type_definition: {
        str out = str_copy(alloc, S("[user_type_definition: "));
        str_dcat(alloc, &out, node->user_type_def.name->symbol.name);
        str_dcat(alloc, &out, S("]"));
        return out;
    }
    }

    fatal("unreachable");
}

str_sized ast_nodes_get_names(allocator *alloc, ast_node_slice nodes) {

    str_sized strings = {.v    = alloc_calloc(alloc, nodes.end - nodes.begin, sizeof strings.v[0]),
                         .size = nodes.end - nodes.begin};

    for (u32 i = nodes.begin; i < nodes.end; ++i) strings.v[i - nodes.begin] = ast_node_str(nodes.v[i]);

    return strings;
}

//

struct ast_symbol *ast_node_sym(ast_node *node) {
    assert(node->tag == ast_symbol || node->tag == ast_string || node->tag == ast_char);
    return &node->symbol;
}

struct ast_arrow *ast_node_arrow(ast_node *node) {
    assert(node->tag == ast_arrow);
    return &node->arrow;
}

struct ast_assignment *ast_node_assignment(ast_node *node) {
    assert(node->tag == ast_assignment || node->tag == ast_reassignment ||
           node->tag == ast_reassignment_op);
    return &node->assignment;
}

struct ast_bool *ast_node_bool(ast_node *node) {
    assert(node->tag == ast_bool);
    return &node->bool_;
}

struct ast_i64 *ast_node_i64(ast_node *node) {
    assert(node->tag == ast_i64);
    return &node->i64;
}

struct ast_u64 *ast_node_u64(ast_node *node) {
    assert(node->tag == ast_u64);
    return &node->u64;
}

struct ast_f64 *ast_node_f64(ast_node *node) {
    assert(node->tag == ast_f64);
    return &node->f64;
}

struct ast_array *ast_node_arr(ast_node *node) {
    if (!TL_AST_HAS_ARRAY(node->tag)) fatal("ast_node_arr called on non-array variant");
    return &node->array;
}

struct ast_lambda_function *ast_node_lf(ast_node *node) {
    assert(node->tag == ast_lambda_function);
    return &node->lambda_function;
}

struct ast_let_in *ast_node_let_in(ast_node *node) {
    assert(node->tag == ast_let_in);
    return &node->let_in;
}

struct ast_let *ast_node_let(ast_node *node) {
    assert(node->tag == ast_let);
    return &node->let;
}

struct ast_if_then_else *ast_node_ifthen(ast_node *node) {
    assert(node->tag == ast_if_then_else);
    return &node->if_then_else;
}

struct ast_lambda_application *ast_node_lambda(ast_node *node) {
    assert(node->tag == ast_lambda_function_application);
    return &node->lambda_application;
}

struct ast_named_application *ast_node_named(ast_node *node) {
    assert(node->tag == ast_named_function_application);
    return &node->named_application;
}

struct ast_tuple *ast_node_tuple(ast_node *node) {
    assert(node->tag == ast_tuple);
    return &node->tuple;
}

struct ast_user_type_def *ast_node_utd(ast_node *node) {
    assert(node->tag == ast_user_type_definition);
    return &node->user_type_def;
}

//

u64 ast_node_hash(ast_node const *self) {
    if (!self) return hash64(null, 0);
    u64 hash = hash64((byte *)&self->tag, sizeof self->tag);

#define combine_node(node)                                                                                 \
    do {                                                                                                   \
        u64 h = ast_node_hash(node);                                                                       \
        hash  = hash64_combine(hash, (void *)&h, sizeof h);                                                \
    } while (0)

    if (TL_AST_HAS_ARRAY(self->tag))
        for (u32 i = 0; i < self->array.n; ++i) combine_node(self->array.nodes[i]);

    switch (self->tag) {
    case ast_attribute_set:
        for (u32 i = 0; i < self->attribute_set.n; i++) combine_node(self->attribute_set.nodes[i]);
        break;

    case ast_continue:
    case ast_nil:
    case ast_void:
    case ast_ellipsis:
    case ast_eof:      break;

    case ast_arrow:    {
        //
        combine_node(self->arrow.left);
        combine_node(self->arrow.right);

        u32        argc = self->arrow.n_type_parameters;
        ast_node **argv = self->arrow.type_parameters;
        for (u32 i = 0; i < argc; i++) combine_node(argv[i]);

    } break;

    case ast_assignment:
    case ast_reassignment:
    case ast_reassignment_op:
        //
        combine_node(self->assignment.name);
        combine_node(self->assignment.value);
        combine_node(self->assignment.op);
        break;

    case ast_bool:
        //
        hash = hash64_combine(hash, (byte *)&self->bool_.val, sizeof(self->bool_.val));
        break;

    case ast_f64:
        //
        hash = hash64_combine(hash, (byte *)&self->f64.val, sizeof(self->f64.val));
        break;

    case ast_i64:
        //
        hash = hash64_combine(hash, (byte *)&self->i64.val, sizeof(self->i64.val));
        break;

    case ast_if_then_else:
        combine_node(self->if_then_else.condition);
        combine_node(self->if_then_else.yes);
        combine_node(self->if_then_else.no);
        break;

    case ast_let_in:
        combine_node(self->let_in.name);
        combine_node(self->let_in.value);
        combine_node(self->let_in.body);
        break;

    case ast_string:
    case ast_symbol:
    case ast_char:   {
        hash = str_hash64_combine(hash, self->symbol.name);
        combine_node(self->symbol.attributes);
    } break;

    case ast_hash_command: hash = str_hash64_combine_sized(hash, self->hash_command.words); break;

    case ast_u64:
        //
        hash = hash64_combine(hash, (byte *)&self->u64.val, sizeof(self->u64.val));
        break;

    case ast_i64_z:
        //
        hash = hash64_combine(hash, (byte *)&self->i64_z.val, sizeof(self->i64_z.val));
        break;

    case ast_u64_zu:
        //
        hash = hash64_combine(hash, (byte *)&self->u64_zu.val, sizeof(self->u64_zu.val));
        break;

    case ast_type_alias:
        //
        combine_node(self->type_alias.name);
        combine_node(self->type_alias.target);
        break;

    case ast_type_predicate:
        //
        combine_node(self->type_predicate.lhs);
        combine_node(self->type_predicate.rhs);
        break;

    case ast_trait_definition:
        //
        combine_node(self->trait_def.name);
        break;

    case ast_user_type_definition:
        //
        combine_node(self->user_type_def.name);
        break;

    case ast_lambda_function:
        //
        combine_node(self->lambda_function.body);
        combine_node(self->lambda_function.attributes);
        break;

    case ast_lambda_function_application: combine_node(self->lambda_application.lambda); break;

    case ast_let:
        for (u32 i = 0; i < self->let.n_type_parameters; ++i) combine_node(self->let.type_parameters[i]);
        combine_node(self->let.name);
        combine_node(self->let.body);
        break;

    case ast_named_function_application:
        for (u32 i = 0; i < self->named_application.n_type_arguments; ++i)
            combine_node(self->named_application.type_arguments[i]);
        combine_node(self->named_application.name);
        break;

    case ast_body:
        forall(i, self->body.expressions) combine_node(self->body.expressions.v[i]);
        forall(i, self->body.defers) combine_node(self->body.defers.v[i]);
        break;

    case ast_case:
        combine_node(self->case_.expression);
        combine_node(self->case_.binary_predicate);
        combine_node(self->case_.union_annotation);
        forall(i, self->case_.conditions) combine_node(self->case_.conditions.v[i]);
        forall(i, self->case_.arms) combine_node(self->case_.arms.v[i]);
        break;

    case ast_binary_op:
        combine_node(self->binary_op.op);
        combine_node(self->binary_op.left);
        combine_node(self->binary_op.right);
        break;

    case ast_unary_op:
        combine_node(self->unary_op.op);
        combine_node(self->unary_op.operand);
        break;

    case ast_return:
        //
        combine_node(self->return_.value);
        break;

    case ast_try:
        //
        combine_node(self->try_.operand);
        break;

    case ast_while:
        combine_node(self->while_.condition);
        combine_node(self->while_.update);
        combine_node(self->while_.body);
        break;

    case ast_tuple: break;
    }

    return hash;
}

//

int ast_node_is_arrow(ast_node const *self) {
    return ast_arrow == self->tag;
}

int ast_node_is_let_in_lambda(ast_node const *self) {
    return ast_let_in == self->tag && ast_lambda_function == self->let_in.value->tag;
}

int ast_node_is_nfa(ast_node const *self) {
    return ast_named_function_application == self->tag;
}

int ast_node_is_nil(ast_node const *self) {
    return ast_nil == self->tag;
}
int ast_node_is_void(ast_node const *self) {
    return ast_void == self->tag;
}
int ast_node_is_nil_or_void(ast_node const *self) {
    return ast_node_is_nil(self) || ast_node_is_void(self);
}
int ast_node_is_symbol(ast_node const *self) {
    return self && ast_symbol == self->tag;
}
int ast_node_is_string(ast_node const *self) {
    return self && ast_string == self->tag;
}
int ast_node_is_try(ast_node const *self) {
    return ast_try == self->tag;
}
int ast_node_is_tuple(ast_node const *self) {
    return ast_tuple == self->tag;
}
int ast_node_is_type_alias(ast_node const *self) {
    return ast_type_alias == self->tag;
}
int ast_node_is_type_predicate(ast_node const *self) {
    return ast_type_predicate == self->tag;
}
int ast_node_is_let(ast_node const *self) {
    return ast_let == self->tag;
}
int ast_node_is_let_in(ast_node const *self) {
    return ast_let_in == self->tag;
}
int ast_node_is_trait_def(ast_node const *self) {
    return ast_trait_definition == self->tag;
}
int ast_node_is_utd(ast_node const *self) {
    return ast_user_type_definition == self->tag;
}
int ast_node_is_enum_def(ast_node const *self) {
    return ast_node_is_utd(self) && self->user_type_def.n_fields &&
           self->user_type_def.field_annotations == null;
}
int ast_node_is_unary_op(ast_node const *self) {
    return ast_unary_op == self->tag;
}
int ast_node_is_union_def(ast_node const *self) {
    return ast_node_is_utd(self) && self->user_type_def.is_union;
}

int ast_node_is_lambda_function(ast_node const *self) {
    return ast_lambda_function == self->tag;
}
int ast_node_is_lambda_application(ast_node const *self) {
    return ast_lambda_function_application == self->tag;
}
int ast_node_is_assignment(ast_node const *self) {
    // Usually this is what we want, since ast_assignment nodes share the same structure.
    return ast_assignment == self->tag || ast_reassignment == self->tag || ast_reassignment_op == self->tag;
}
int ast_node_is_reassignment(ast_node const *self) {
    // Sometimes we need to explicitly filter on reassignment nodes semantically.
    return ast_reassignment == self->tag;
}
int ast_node_is_binary_op(ast_node const *self) {
    return ast_binary_op == self->tag;
}
int ast_node_is_binary_op_struct_access(ast_node const *self) {
    if (!ast_node_is_binary_op(self)) return 0;
    str op = ast_node_str(self->binary_op.op);
    return is_struct_access_operator(str_cstr(&op));
}
int ast_node_is_body(ast_node const *self) {
    return ast_body == self->tag;
}
int ast_node_is_case(ast_node const *self) {
    return ast_case == self->tag;
}
int ast_node_is_hash_command(ast_node const *self) {
    return ast_hash_command == self->tag;
}
int ast_node_is_ifc_block(ast_node const *self) {
    return ast_node_is_hash_command(self) && self->hash_command.is_c_block;
}

int ast_node_is_std_application(ast_node const *self) {
    if (!ast_node_is_nfa(self)) return 0;
    return (0 == str_cmp_nc(ast_node_str(self->named_application.name), "std_", 4));
}

int ast_node_is_specialized(ast_node const *self) {
    if (ast_node_is_nfa(self)) return self->named_application.is_specialized;
    if (ast_node_is_lambda_application(self)) return self->lambda_application.is_specialized;
    if (ast_node_is_let(self)) return self->let.is_specialized;
    return 0;
}

void ast_node_set_is_specialized(ast_node *self) {
    if (ast_node_is_nfa(self)) self->named_application.is_specialized = 1;
    else if (ast_node_is_lambda_application(self)) self->lambda_application.is_specialized = 1;
    else if (ast_node_is_let(self)) self->let.is_specialized = 1;
    else fatal("logic error");
}

ast_node_sized ast_node_sized_from_ast_array(ast_node *node) {
    return (ast_node_sized){.size = node->array.n, .v = node->array.nodes};
}
ast_node_sized ast_node_sized_from_ast_array_const(ast_node const *node) {
    return (ast_node_sized){.size = node->array.n, .v = node->array.nodes};
}

ast_node_sized ast_let_type_params(ast_node const *node) {
    assert(ast_node_is_let(node));
    ast_node **tp = node->let.type_parameters;
    u32        n  = node->let.n_type_parameters;
    if (!n && node->let.name->symbol.annotation) {
        ast_node *ann = node->let.name->symbol.annotation;
        if (ast_node_is_arrow(ann) && ann->arrow.n_type_parameters > 0) {
            tp = ann->arrow.type_parameters;
            n  = ann->arrow.n_type_parameters;
        }
    }
    return (ast_node_sized){.size = n, .v = tp};
}

// -- lambda closure attributes --

lambda_closure_attrs lambda_get_closure_attrs(allocator *alloc, ast_node *attributes) {
    lambda_closure_attrs out = {0};
    if (!attributes) return out;

    assert(attributes->tag == ast_attribute_set);
    struct ast_attribute_set *attrs = &attributes->attribute_set;

    for (u32 i = 0; i < attrs->n; ++i) {
        ast_node *node = attrs->nodes[i];
        str name = ast_node_is_nfa(node) ? ast_node_str(node->named_application.name) : ast_node_str(node);

        if (str_eq(name, S("alloc"))) {
            out.has_alloc = 1;
            if (ast_node_is_nfa(node) && node->named_application.n_arguments > 0) {
                out.alloc_expr = node->named_application.arguments[0];
            }
        } else if (str_eq(name, S("capture"))) {
            out.has_capture = 1;
            if (ast_node_is_nfa(node)) {
                u8 n = node->named_application.n_arguments;
                if (n > 0) {
                    out.capture_names   = alloc_malloc(alloc, n * sizeof(str));
                    out.capture_nodes   = alloc_malloc(alloc, n * sizeof(ast_node *));
                    out.n_capture_names = n;
                    for (u8 j = 0; j < n; ++j) {
                        out.capture_nodes[j] = node->named_application.arguments[j];
                        out.capture_names[j] = ast_node_str(out.capture_nodes[j]);
                    }
                }
            }
        }
    }

    return out;
}

//

hashmap *ast_node_str_map_create(allocator *alloc, u32 n) {
    return map_create(alloc, sizeof(ast_node *), n);
}

void ast_node_str_map_destroy(hashmap **p) {
    map_destroy(p);
}

void ast_node_str_map_add(hashmap **p, str key, ast_node *val) {
    str_map_set(p, key, &val);
}

void ast_node_str_map_erase(hashmap *map, str key) {
    str_map_erase(map, key);
}

ast_node *ast_node_str_map_get(hashmap *map, str key) {
    ast_node **found = str_map_get(map, key);
    return found ? *found : null;
}

ast_node *ast_node_str_map_iter(hashmap *map, hashmap_iterator *iter) {
    if (map_iter(map, iter)) {
        return *(ast_node **)iter->data;
    }
    return null;
}
