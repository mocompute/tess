#ifndef TESS_AST_H
#define TESS_AST_H

#include "alloc.h"
#include "array.h"
#include "ast_tags.h"
#include "error.h"
#include "hashmap.h"
#include "nodiscard.h"
#include "str.h"
#include "type.h"

defarray(ast_node_array, struct ast_node *);
defsized(ast_node_sized, struct ast_node *);
defslice(ast_node_slice, struct ast_node *);

typedef struct ast_node {
    union {
        struct ast_symbol {
            str              name;
            str              original;
            struct ast_node *annotation;
            tl_polytype     *annotation_type;
            int              is_mangled; // by parser for module namespacing
        } symbol;

        struct ast_bool {
            int val;
        } bool_;

        struct ast_i64 {
            i64 val;
        } i64;

        struct ast_u64 {
            u64 val;
        } u64;

        struct ast_f64 {
            f64 val;
        } f64;

        struct ast_arrow {
            struct ast_node *left;
            struct ast_node *right;
        } arrow;

        struct ast_assignment {
            struct ast_node *name;
            struct ast_node *value;
            int              is_field_name;
        } assignment;

        struct ast_array {
            // all variants with arrays must use this layout
            struct ast_node **nodes;
            u8                n;
        } array;

        struct ast_lambda_function {
            struct ast_node **parameters;
            u8                n_parameters;
            struct ast_node  *body;
        } lambda_function;

        struct ast_let {
            struct ast_node **parameters;
            u8                n_parameters;
            struct ast_node  *name;
            struct ast_node  *body;
        } let;

        struct ast_lambda_application {
            struct ast_node **arguments;
            u8                n_arguments;
            struct ast_node  *lambda;
            int               is_specialized;
        } lambda_application;

        struct ast_named_application {
            struct ast_node **arguments;
            u8                n_arguments;
            struct ast_node  *name;
            int               is_specialized;
        } named_application;

        struct ast_tuple {
            struct ast_node **elements;
            u8                n_elements;
        } tuple;

        // below have no array

        struct ast_body {
            ast_node_sized expressions;
        } body;

        struct ast_if_then_else {
            struct ast_node *condition;
            struct ast_node *yes;
            struct ast_node *no;
        } if_then_else;

        struct ast_return {
            struct ast_node *value;
            int              is_break_statement;
        } return_;

        struct ast_while {
            struct ast_node *condition;
            struct ast_node *update;
            struct ast_node *body;
        } while_;

        struct ast_binary_op {
            struct ast_node *left;
            struct ast_node *right;
            struct ast_node *op;
        } binary_op;

        struct ast_unary_op {
            struct ast_node *operand;
            struct ast_node *op;
        } unary_op;

        struct ast_let_in {
            struct ast_node *name;
            struct ast_node *value;
            struct ast_node *body;
        } let_in;

        struct ast_user_type_def {
            struct ast_node  *name;
            struct ast_node **type_arguments;
            struct ast_node **field_annotations; // if null from parser, this is an enum.
            struct ast_node **field_names;       // contains enum names
            tl_monotype     **field_types;
            u8                n_fields;
            u8                n_type_arguments;
            u8                is_union;
        } user_type_def;

        struct ast_hash_command {
            str       full; // must be first, as in ast_symbol
            str_sized words;
            int       is_c_block;
        } hash_command;

        struct ast_type_alias {
            struct ast_node *name;   // symbol or nfa
            struct ast_node *target; // symbol or nfa
        } type_alias;
    };

    char const       *file;
    u32               line;
    u32               col;
    tl_polytype      *type;
    ast_tag           tag;
    enum tl_error_tag error;
} ast_node;

// -- iterator functions --

typedef void (*ast_op_fun)(void *, ast_node *);
typedef ast_node *(*ast_op_map_fun)(void *, ast_node *);
typedef void (*ast_op_cfun)(void *, ast_node const *);

// -- variant accessors --

struct ast_address_of         *ast_node_address_of(ast_node *);
struct ast_arrow              *ast_node_arrow(ast_node *);
struct ast_assignment         *ast_node_assignment(ast_node *);
struct ast_bool               *ast_node_bool(ast_node *);
struct ast_dereference        *ast_node_deref(ast_node *);
struct ast_dereference_assign *ast_node_deref_assign(ast_node *);
struct ast_i64                *ast_node_i64(ast_node *);
struct ast_u64                *ast_node_u64(ast_node *);
struct ast_f64                *ast_node_f64(ast_node *);
struct ast_array              *ast_node_arr(ast_node *);
struct ast_lambda_function    *ast_node_lf(ast_node *);
struct ast_let_in             *ast_node_let_in(ast_node *);
struct ast_let_match_in       *ast_node_let_match_in(ast_node *);
struct ast_let                *ast_node_let(ast_node *);
struct ast_if_then_else       *ast_node_ifthen(ast_node *);
struct ast_lambda_application *ast_node_lambda(ast_node *);
struct ast_named_application  *ast_node_named(ast_node *);
struct ast_symbol             *ast_node_sym(ast_node *);
struct ast_tuple              *ast_node_tuple(ast_node *);
struct ast_user_type_def      *ast_node_utd(ast_node *);

// -- ast_node --

nodiscard ast_node *ast_node_create(allocator *, ast_tag) mallocfun;
nodiscard ast_node *ast_node_create_i64(allocator *, i64) mallocfun;
nodiscard ast_node *ast_node_create_u64(allocator *, u64) mallocfun;
nodiscard ast_node *ast_node_create_f64(allocator *, f64) mallocfun;
nodiscard ast_node *ast_node_create_arrow(allocator *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_assignment(allocator *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_body(allocator *, ast_node_sized) mallocfun;
nodiscard ast_node *ast_node_create_bool(allocator *, int) mallocfun;
nodiscard ast_node *ast_node_create_continue(allocator *) mallocfun;
nodiscard ast_node *ast_node_create_nil(allocator *) mallocfun;
nodiscard ast_node *ast_node_create_if_then_else(allocator *, ast_node *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_binary_op(allocator *, ast_node *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_unary_op(allocator *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_return(allocator *, ast_node *, int) mallocfun;
nodiscard ast_node *ast_node_create_while(allocator *, ast_node *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_let_in(allocator *, ast_node *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_let(allocator *, ast_node *, ast_node_sized, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_nfa(allocator *, ast_node *, ast_node_sized) mallocfun;
nodiscard ast_node *ast_node_create_lfa(allocator *, ast_node *, ast_node_sized) mallocfun;
nodiscard ast_node *ast_node_create_tuple(allocator *, ast_node_sized) mallocfun;
nodiscard ast_node *ast_node_create_type_alias(allocator *, ast_node *, ast_node *) mallocfun;
nodiscard ast_node *ast_node_create_sym(allocator *alloc, str str); // copies str
nodiscard ast_node *ast_node_create_sym_c(allocator *, char const *);
nodiscard ast_node *ast_node_clone(allocator *, ast_node const *) mallocfun;
void                ast_node_move(ast_node *dst, ast_node *src);

str                 ast_node_str(ast_node const *);
str                 ast_node_name_original(ast_node const *);
void                ast_node_name_replace(ast_node *, str);
ast_node           *ast_node_lvalue(ast_node *);
ast_node           *ast_node_op_rightmost(ast_node *);
void                ast_node_type_set(ast_node *, tl_polytype *);

// -- traversal --

typedef void (*ast_node_each_node_fun)(void *ctx, ast_node *);
typedef ast_node *(*ast_node_map_node_fun)(void *ctx, ast_node *);

void ast_node_each_node(void *, ast_node_each_node_fun, ast_node *);
void ast_node_map_node(void *, ast_node_map_node_fun, ast_node *);

// -- arguments

typedef struct {
    ast_node_sized nodes;
    u32            index;
    u32            count;
} ast_arguments_iter;

ast_arguments_iter ast_node_arguments_iter(ast_node *);      // smart selection based on node tag
ast_node          *ast_arguments_next(ast_arguments_iter *); // recognises nil argument

ast_node          *ast_node_body(ast_node *); // body of let or let in lambda.

// -- utilities --

str            v2_ast_node_to_string(allocator *, ast_node const *);

char const    *ast_tag_to_string(ast_tag);

str_sized      ast_nodes_get_names(allocator *, ast_node_slice);

void           ast_node_dfs(void *, ast_node *, ast_op_fun);
void           ast_node_cdfs(void *, ast_node const *, ast_op_cfun);
void           ast_node_dfs_safe_for_recur(allocator *, void *, ast_node *, ast_op_fun);
ast_node      *ast_node_map_dfs_safe_for_recur(allocator *, void *, ast_node *, ast_op_map_fun);

ast_node     **ast_node_assignment_names(allocator *, ast_node const *);

u64            ast_node_hash(ast_node const *);

int            ast_node_is_arrow(ast_node const *);
int            ast_node_is_assignment(ast_node const *);
int            ast_node_is_binary_op(ast_node const *);
int            ast_node_is_binary_op_struct_access(ast_node const *);
int            ast_node_is_body(ast_node const *);
int            ast_node_is_hash_command(ast_node const *);
int            ast_node_is_ifc_block(ast_node const *);
int            ast_node_is_lambda_function(ast_node const *);
int            ast_node_is_lambda_application(ast_node const *);
int            ast_node_is_let(ast_node const *);
int            ast_node_is_let_in(ast_node const *);
int            ast_node_is_let_in_lambda(ast_node const *);
int            ast_node_is_nfa(ast_node const *);
int            ast_node_is_nil(ast_node const *);
int            ast_node_is_symbol(ast_node const *);
int            ast_node_is_tuple(ast_node const *);
int            ast_node_is_type_alias(ast_node const *);
int            ast_node_is_utd(ast_node const *);
int            ast_node_is_enum_def(ast_node const *);
int            ast_node_is_union_def(ast_node const *);
int            ast_node_is_std_application(ast_node const *);
int            ast_node_is_specialized(ast_node const *);

void           ast_node_set_is_specialized(ast_node *);

ast_node_sized ast_node_sized_from_ast_array(ast_node *);
ast_node_sized ast_node_sized_from_ast_array_const(ast_node const *);

// -- hashmap: str => ast_node* --

nodiscard hashmap *ast_node_str_map_create(allocator *, u32) mallocfun;
void               ast_node_str_map_destroy(hashmap **);
void               ast_node_str_map_add(hashmap **, str, ast_node *);
void               ast_node_str_map_erase(hashmap *, str);
ast_node          *ast_node_str_map_get(hashmap *, str);
ast_node          *ast_node_str_map_iter(hashmap *, hashmap_iterator *);

#endif
