#include "ast.h"
#include "ast_tags.h"
#include "error.h"
#include "parser_internal.h"

#include <errno.h>

int is_arithmetic_operator(char const *s) {
    static char const *strings[] = {
      "+", "-", "*", "/", "%", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_assignment_by_operator(char const *s) {
    static char const *strings[] = {
      "+=", "-=", "*=", "/=", "%=", "<<=", ">>=", "&=", "^=", "|=", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_relational_operator(char const *s) {
    static char const *strings[] = {
      "<", "<=", "==", "!=", ">=", ">", null,
    };
    char const **it = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_logical_operator(char const *s) {
    static char const *strings[] = {"&&", "||", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int is_bitwise_operator(char const *s) {
    static char const *strings[] = {"&", "|", "^", "<<", ">>", null};
    char const       **it        = strings;
    while (*it != null)
        if (0 == strcmp(*it++, s)) return 1;
    return 0;
}

int a_assignment_by_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {
    case tok_symbol:
        if (is_assignment_by_operator(self->token.s)) {
            op = self->token.s;
        } else return 1;
        break;

    case tok_bang_equal:
    case tok_star:
    case tok_dot:
    case tok_equal_equal:
    case tok_logical_and:
    case tok_logical_or:
    case tok_arrow:
    case tok_ampersand:
    case tok_plus:
    case tok_minus:
    case tok_bar:
    case tok_bang:
    case tok_comma:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_double_colon:
    case tok_semicolon:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_open_square:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_s_string:
    case tok_f_string_start:
    case tok_f_string_mid:
    case tok_f_string_end:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

int a_binary_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {
    case tok_symbol:
        if (is_arithmetic_operator(self->token.s) || is_logical_operator(self->token.s) ||
            is_relational_operator(self->token.s) || is_bitwise_operator(self->token.s)) {
            op = self->token.s;
        } else return 1;
        break;

    case tok_bang_equal:          op = "!="; break;
    case tok_star:                op = "*"; break;
    case tok_dot:                 op = "."; break;
    case tok_equal_equal:         op = "=="; break;
    case tok_logical_and:         op = "&&"; break;
    case tok_logical_or:          op = "||"; break;
    case tok_arrow:               op = "->"; break;
    case tok_ampersand:           op = "&"; break;
    case tok_open_square:         op = "["; break;
    case tok_plus:                op = "+"; break;
    case tok_minus:               op = "-"; break;
    case tok_bar:                 op = "|"; break;
    case tok_double_colon:        op = "::"; break;

    case tok_bang:
    case tok_comma:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_semicolon:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_s_string:
    case tok_f_string_start:
    case tok_f_string_mid:
    case tok_f_string_end:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 0);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

int a_unary_operator(parser *self, int min_prec) {
    if (next_token(self)) return 1;

    char const *op = null;
    switch (self->token.tag) {

    case tok_bang:  op = "!"; break;
    case tok_plus:  op = "+"; break;
    case tok_minus: op = "-"; break;

    case tok_symbol:
        if (is_unary_operator(self->token.s)) op = self->token.s;
        else return 1;
        break;

    case tok_star:
    case tok_ampersand:
    case tok_bang_equal:
    case tok_bar:
    case tok_comma:
    case tok_dot:
    case tok_c_block:
    case tok_colon:
    case tok_colon_equal:
    case tok_double_colon:
    case tok_semicolon:
    case tok_logical_and:
    case tok_logical_or:
    case tok_arrow:
    case tok_ellipsis:
    case tok_open_round:
    case tok_close_round:
    case tok_open_curly:
    case tok_close_curly:
    case tok_open_square:
    case tok_close_square:
    case tok_double_open_square:
    case tok_double_close_square:
    case tok_equal_sign:
    case tok_equal_equal:
    case tok_invalid:
    case tok_number:
    case tok_string:
    case tok_c_string:
    case tok_s_string:
    case tok_f_string_start:
    case tok_f_string_mid:
    case tok_f_string_end:
    case tok_char:
    case tok_comment:
    case tok_hash_command:        return 1;
    }

    if (!op) return 1;

    int prec = operator_precedence(op, 1);
    if (prec < min_prec) return 1;

    return result_ast_node(self, ast_node_create_sym_c(self->ast_arena, op));
}

// Parse arity qualifier: /N where N is a non-negative integer.
// Used for function references with explicit type args: name[TypeArgs]/N
int a_arity_qualifier(parser *self) {
    if (next_token(self)) return 1;
    if (tok_symbol != self->token.tag || 0 != strcmp("/", self->token.s)) return 1;

    if (next_token(self)) return 1;
    if (tok_number != self->token.tag) return 1;

    // Arities are plain non-negative integers only (no suffixes, no floats)
    char *end;
    errno  = 0;
    long n = strtol(self->token.s, &end, 10);
    if (errno || end == self->token.s || *end != '\0' || n < 0 || n > 255) return 1;
    return result_ast_i64(self, (i64)n);
}

int a_attribute_set(parser *self) {
    if (a_try(self, a_double_open_square)) return 1;

    ast_node_array items = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_double_close_square)) goto done;

    if (0 == a_try(self, a_funcall) || 0 == a_try(self, a_identifier)) {
        array_push(items, self->result);
    }

    while (1) {
        if (0 == a_try(self, a_double_close_square)) break;
        if (a_try(self, a_comma)) return 1;
        if (0 == a_try(self, a_funcall) || 0 == a_try(self, a_identifier)) {
            array_push(items, self->result);
        }
    }

done:;

    ast_node *out = ast_node_create_attribute_set(self->ast_arena, (ast_node_sized)sized_all(items));
    return result_ast_node(self, out);
}

int a_body_element(parser *self) {
    // Note: statement before expression, because assignment and ident are ambiguous. Commas can be ignored,
    // so they can be used between body elements for readability.

    // commas and semicolons are accepted as body element separators
    int ignore = a_try(self, a_comma) && a_try(self, a_semicolon);
    (void)ignore; // for GCC

    int res;
    if (0 == (res = a_try(self, a_statement))) return 0;
    if (ERROR_STOP == res) return res;

    if (0 == a_try(self, a_expression)) return 0;

    else return 1;
}

int a_while_statement(parser *self) {
    if (a_try_s(self, the_symbol, "while")) return 1;

    ast_node *condition = parse_expression(self, INT_MIN);
    if (!condition) return 1;

    // optional update expression: a command, then expression, before the open curly
    ast_node *update = null;
    if (0 == a_try(self, a_comma)) {
        int res = 0;
        if ((res = a_try(self, a_statement))) return res;
        update = self->result;
    }

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return 1;
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body(self, exprs, defers);

    ast_node *r    = ast_node_create_while(self->ast_arena, condition, update, body);
    return result_ast_node(self, r);
}

int a_for_statement(parser *self) {
    // The `for` statement is sugar over a while statement which uses a defined iterator interface. It has
    // four forms: with or without a Module specifier, and with or without a pointer specifier. If the
    // module specifier is omitted, Array is used as the default. The pointer specifier (using the &
    // operator) indicates the loop variable is a pointer which provides access to the underlying storage.
    //
    //     for x in xs { ... }
    //     for x.& in xs { ... }
    //     for x in Module xs { ... }
    //     for x.& in Module xs { ... }
    //
    // The iterator interface requires modules to implement the following functions:
    //
    //     iter_init    (iterable: Ptr(T)) -> Iter // T is the iterable, and Iter is any type.
    //     iter_value   (Ptr(Iter))        -> TValue
    //     iter_ptr     (Ptr(Iter))        -> Ptr(TValue)
    //     iter_cond    (Ptr(Iter))        -> Bool
    //     iter_update  (Ptr(Iter))        -> Void
    //     iter_deinit  (Ptr(Iter))        -> Void
    //
    // See <Array.tl> for a sample implementation
    //
    // The statement `for x in Module xs { ... }` desugars into:
    //
    //     iter := Module.iter_init(xs.&)
    //     while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
    //       x := Module.iter_value(iter.&)
    //       ...
    //     }
    //     Module.iter_deinit(iter.&)
    //
    // The statement `for x.& in Module xs { ... }` desugars into:
    //
    //     iter := Module.iter_init(xs.&)
    //     while Module.iter_cond(iter.&), Module.iter_update(iter.&) {
    //       x := Module.iter_ptr(iter.&)
    //       ...
    //     }
    //     Module.iter_deinit(iter.&)
    //

    if (a_try_s(self, the_symbol, "for")) return 1;
    ast_node *variable = parse_expression(self, INT_MIN);
    if (!variable || (!ast_node_is_symbol(variable) && !ast_node_is_unary_op(variable))) return 1;

    int is_pointer = 0;
    if (ast_node_is_unary_op(variable)) {
        if (is_ampersand(variable->unary_op.op)) is_pointer = 1;
        else return 1; // no other unary op is valid

        // reset variable to the actual symbol
        variable = variable->unary_op.operand;
        if (!ast_node_is_symbol(variable)) return 1;
    }

    if (a_try_s(self, the_symbol, "in")) return 1;

    // one or two expressions can follow before the open brace

    ast_node *first     = parse_expression(self, INT_MIN);
    ast_node *second    = null;

    int       saw_curly = 0;
    if (a_try(self, a_open_curly)) second = parse_expression(self, INT_MIN);
    else saw_curly = 1;

    ast_node *module   = null;
    ast_node *iterable = null;
    if (!second) {
        // either second failed to parse, or we saw a curly
        if (!saw_curly) return 1;
        iterable = first;
    } else {
        // we saw two expressions, make sure they meet our requirements

        // first expression must be a symbol because it's a module name
        if (!ast_node_is_symbol(first)) return 1;
        module = first;

        // second can be anything
        iterable = second;
    }

    if (module) {
        assert(!saw_curly);
        // Now we need the open curly
        if (a_try(self, a_open_curly)) return 1;
    }

    // Read body of for loop
    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return 1;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }
    ast_node *user_body = create_body(self, exprs, defers);

    // Construct enclosing let-in(s) for the body of the loop. For value iteration, we need two let-ins:
    // first to grab the iterator pointer, and second to set the value variable. For pointer iteration, we
    // just need one let-in: to grab the iterator pointer and set the value variable.

    // First, we need a name for the hidden iterator variable
    ast_node *iterator = ast_node_create_sym(self->ast_arena, next_var_name(self));

    // And we need an address-of operation for the iterator and the iterable
    ast_node *address_of       = ast_node_create_sym_c(self->ast_arena, "&");
    ast_node *iterator_address = ast_node_create_unary_op(self->ast_arena, address_of, iterator);
    ast_node *iterable_address = ast_node_create_unary_op(self->ast_arena, address_of, iterable);

    // Do we use default Array module, or a user-provided module?
    str module_name;
    if (module) module_name = ast_node_str(module);
    else module_name = S("Array");

    // Create the nfa for iter_init
    ast_node *call_iter_init = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterable_address;
        ast_node *iter_init      = ast_node_create_sym_c(self->ast_arena, "iter_init__1");
        mangle_name_for_module(self, iter_init, module_name);
        call_iter_init = ast_node_create_nfa(self->ast_arena, iter_init, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_value
    ast_node *call_iter_value = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_value     = ast_node_create_sym_c(self->ast_arena, "iter_value__1");
        mangle_name_for_module(self, iter_value, module_name);
        call_iter_value = ast_node_create_nfa(self->ast_arena, iter_value, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_ptr
    ast_node *call_iter_ptr = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_ptr       = ast_node_create_sym_c(self->ast_arena, "iter_ptr__1");
        mangle_name_for_module(self, iter_ptr, module_name);
        call_iter_ptr = ast_node_create_nfa(self->ast_arena, iter_ptr, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_cond
    ast_node *call_iter_cond = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_cond      = ast_node_create_sym_c(self->ast_arena, "iter_cond__1");
        mangle_name_for_module(self, iter_cond, module_name);
        call_iter_cond = ast_node_create_nfa(self->ast_arena, iter_cond, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_update
    ast_node *call_iter_update = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_update    = ast_node_create_sym_c(self->ast_arena, "iter_update__1");
        mangle_name_for_module(self, iter_update, module_name);
        call_iter_update =
          ast_node_create_nfa(self->ast_arena, iter_update, (ast_node_sized){0}, iter_args);
    }

    // Create the nfa for iter_deinit
    ast_node *call_iter_deinit = null;
    {
        ast_node_sized iter_args = {.size = 1, .v = alloc_malloc(self->ast_arena, sizeof(iter_args.v[0]))};
        iter_args.v[0]           = iterator_address;
        ast_node *iter_deinit    = ast_node_create_sym_c(self->ast_arena, "iter_deinit__1");
        mangle_name_for_module(self, iter_deinit, module_name);
        call_iter_deinit =
          ast_node_create_nfa(self->ast_arena, iter_deinit, (ast_node_sized){0}, iter_args);
    }

    ast_node *while_body = null;
    {
        ast_node *lhs = variable;
        // Make pointer loop variables const: attach Const annotation.
        // Value iteration yields an owned copy — no Const restriction needed.
        if (is_pointer && !lhs->symbol.annotation) {
            lhs->symbol.annotation = ast_node_create_sym_c(self->ast_arena, "Const");
        }
        ast_node *rhs      = is_pointer ? call_iter_ptr : call_iter_value;
        ast_node *for_body = ast_node_create_let_in(self->ast_arena, lhs, rhs, user_body);
        while_body         = for_body;
    }

    // Now we need to construct the while statement. It will be enclosed in a let-in which initializes the
    // iterator. And it will be followed by a funcall to free the iterator. So we will have: let iter = init
    // in body (while, free).

    ast_node_array while_statement_exprs = {.alloc = self->ast_arena};

    ast_node      *condition             = call_iter_cond;
    ast_node      *update                = call_iter_update;
    ast_node      *while_statement = ast_node_create_while(self->ast_arena, condition, update, while_body);

    // The body of the let-in: the while statement followed by the iter_deinit
    array_push(while_statement_exprs, while_statement);
    array_push(while_statement_exprs, call_iter_deinit);
    ast_node *while_statement_exprs_body = create_body(self, while_statement_exprs, (ast_node_array){0});

    ast_node *lhs                        = iterator;
    ast_node *rhs                        = call_iter_init;
    ast_node *let_iter_init = ast_node_create_let_in(self->ast_arena, lhs, rhs, while_statement_exprs_body);

    // The resulting node is a let-in:
    return result_ast_node(self, let_iter_init);
}

int a_break_statement(parser *self) {
    if (a_try_s(self, the_symbol, "break")) return 1;

    ast_node *r = ast_node_create_return(self->ast_arena, null, 1);
    return result_ast_node(self, r);
}

int a_continue_statement(parser *self) {
    if (a_try_s(self, the_symbol, "continue")) return 1;

    ast_node *r = ast_node_create_continue(self->ast_arena);
    return result_ast_node(self, r);
}

int a_return_statement(parser *self) {
    if (a_try_s(self, the_symbol, "return")) return 1;

    ast_node *value = parse_expression(self, INT_MIN);
    if (!value) {
        // allow `return` without an argument to mean `return void`
        result_ast(self, ast_void);
        value = self->result;
    }

    ast_node *r = ast_node_create_return(self->ast_arena, value, 0);
    return result_ast_node(self, r);
}

int a_defer_eligible_statement(parser *self) {
    int res;
    if (0 == (res = a_try(self, a_assignment))) return 0;
    if (ERROR_STOP == res) {
        return res;
    }
    if (0 == a_try(self, a_reassignment)) return 0;
    if (0 == a_try(self, a_while_statement)) return 0;
    if (0 == a_try(self, a_for_statement)) return 0;

    return 1;
}

int a_defer_eligible_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

int a_defer_eligible_body_element(parser *self) {
    // Note: statement before expression, because assignment and ident are ambiguous. Commas can be ignored,
    // so they can be used between body elements for readability.
    int ignore = a_try(self, a_comma);
    (void)ignore; // for GCC
    if (0 == a_try(self, a_defer_eligible_statement) || 0 == a_try(self, a_defer_eligible_expression))
        return 0;
    else return 1;
}

int a_defer_statement(parser *self) {
    if (a_try_s(self, the_symbol, "defer")) return 1;

    // single element
    if (0 == a_try(self, a_defer_eligible_body_element)) return 0;

    // block element
    if (a_try(self, a_open_curly)) return 1;
    ast_node_array exprs = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_defer_eligible_body_element)) return ERROR_STOP; // stop parsing
        array_push(exprs, self->result);
    }
    // cannot nest defer inside defer
    ast_node *body = create_body(self, exprs, (ast_node_array){0});
    return result_ast_node(self, body);
}

int a_reassignment(parser *self) {
    // x = newval
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    ast_node *op = null;
    if (0 == a_try(self, a_equal_sign)) {
        // op stays null
    } else if (0 == a_try_int(self, a_assignment_by_operator, INT_MIN)) {
        op = self->result;
    } else {
        return 1;
    }

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *a = op ? ast_node_create_reassignment_op(self->ast_arena, lval, val, op)
                     : ast_node_create_reassignment(self->ast_arena, lval, val);
    return result_ast_node(self, a);
}

int a_assignment(parser *self) {
    // Note: this is a let-in expression
    ast_node *lval = parse_lvalue(self);
    if (!lval) return 1;

    if (a_try(self, a_colon_equal)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    // Let-else form: s: MySome := val else { diverge-or-value }
    // Desugars to: when val { s: MySome { <remaining body> } else { <else-body> } }
    if (ast_node_is_symbol(lval) && lval->symbol.annotation && 0 == a_try_s(self, the_symbol, "else")) {
        ast_node *else_body = parse_body(self);
        if (!else_body) return ERROR_STOP;

        // Parse remaining body expressions (the continuation after the bail)
        ast_node_array exprs  = {.alloc = self->ast_arena};
        ast_node_array defers = {.alloc = self->ast_arena};
        while (1) {
            if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
            else if (a_try(self, a_body_element)) break;
            else array_push(exprs, self->result);
        }
        ast_node *success_body = create_body_fallback(self, exprs, defers, val);

        // Build case node: condition is the annotated lval, else arm is the bail body
        ast_node_array conditions = {.alloc = self->ast_arena};
        ast_node_array arms       = {.alloc = self->ast_arena};
        array_push(conditions, lval);
        array_push(arms, success_body);

        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_body);

        ast_node *node =
          ast_node_create_case(self->ast_arena, val, (ast_node_sized)array_sized(conditions),
                               (ast_node_sized)array_sized(arms), null, null, AST_TAGGED_UNION_VALUE);
        set_node_file(self, node);
        return result_ast_node(self, node);
    }

    // Normal let-in path
    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) break;
        else array_push(exprs, self->result);
    }

    ast_node *body = create_body_fallback(self, exprs, defers, val);

    ast_node *a    = ast_node_create_let_in(self->ast_arena, lval, val, body);
    return result_ast_node(self, a);
}

int a_statement(parser *self) {
    int res;
    if (0 == (res = a_try(self, a_assignment))) return 0;
    if (ERROR_STOP == res) {
        return res;
    }
    if (0 == a_try(self, a_reassignment)) return 0;
    if (0 == a_try(self, a_while_statement)) return 0;
    if (0 == a_try(self, a_for_statement)) return 0;
    if (0 == a_try(self, a_break_statement)) return 0;
    if (0 == a_try(self, a_continue_statement)) return 0;
    if (0 == a_try(self, a_return_statement)) return 0;

    // add { ... } block statements (creates new lexical scope for a sequence of statements)
    ast_node *block_statement = parse_body(self);
    if (block_statement) {
        result_ast_node(self, block_statement);
        return 0;
    }

    return 1;
}

ast_node *maybe_wrap_lambda_function_in_let_in(parser *self, ast_node *node) {
    // Note: special case to handle anonymous lambdas as function arguments: the transpiler requires every
    // lambda to be named, because the function is hoisted to a toplevel function. So here we wrap a lambda
    // function in a simple `let gen_name = f in f` form.
    if (!ast_node_is_lambda_function(node)) return node;

    ast_node *lval = ast_node_create_sym(self->ast_arena, next_var_name(self));
    ast_node *val  = node;

    // body of let: just the symbol referring to the lambda's name
    ast_node_array exprs = {.alloc = self->ast_arena};
    array_push(exprs, lval);
    ast_node *body = create_body(self, exprs, (ast_node_array){0});

    ast_node *a    = ast_node_create_let_in(self->ast_arena, lval, val, body);
    return a;
}

ast_node *create_body(parser *self, ast_node_array exprs, ast_node_array defers) {
    // Wrap bare lambdas in let-in bindings so they get named and hoisted.
    forall(i, exprs) {
        exprs.v[i] = maybe_wrap_lambda_function_in_let_in(self, exprs.v[i]);
    }
    array_shrink(exprs);
    ast_node *body    = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));
    body->body.defers = (ast_node_sized)sized_all(defers);
    set_node_file(self, body);
    return body;
}

ast_node *create_body_fallback(parser *self, ast_node_array exprs, ast_node_array defers,
                               ast_node *fallback) {
    if (0 == exprs.size && fallback) array_push(exprs, fallback);
    return create_body(self, exprs, defers);
}

int a_lambda_function(parser *self) {
    ast_node_array params     = {.alloc = self->ast_arena};

    ast_node      *attributes = null;
    if (0 == a_try(self, a_attribute_set)) {
        attributes = self->result;
    }

    if (a_try(self, a_open_round)) return 1;
    if (0 == a_try(self, a_close_round)) goto decl_done;
    if (0 == a_try(self, a_param)) array_push(params, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto decl_done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_param)) return 1;
        array_push(params, self->result);
    }

decl_done:

    if (a_try(self, a_open_curly)) return 1;

    ast_node_array exprs = {.alloc = self->ast_arena};

    while (1) {
        if (0 == a_try(self, a_close_curly)) break;
        if (a_try(self, a_body_element)) return 1;
        array_push(exprs, self->result);
    }

    array_shrink(exprs);
    ast_node *body = ast_node_create_body(self->ast_arena, (ast_node_sized)sized_all(exprs));
    set_node_file(self, body);

    ast_node *l = ast_node_create(self->ast_arena, ast_lambda_function);
    set_node_parameters(self, l, &params);
    l->lambda_function.body       = body;
    l->lambda_function.attributes = attributes;
    return result_ast_node(self, l);
}

int a_lambda_funcall(parser *self) {
    if (a_try(self, a_lambda_function)) return 1;
    ast_node *lambda = self->result;

    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_expression)) array_push(args, self->result);

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_expression)) return 1;
        array_push(args, self->result);
    }

done:;

    ast_node *node = ast_node_create_lfa(self->ast_arena, lambda, (ast_node_sized)array_sized(args));
    return result_ast_node(self, node);
}

int a_value(parser *self) {
    // Put funcall before type constructor, due to arity mangling
    if (0 == a_try(self, a_funcall)) return 0;
    if (0 == a_try(self, a_type_constructor)) return 0;
    if (0 == a_try(self, a_lambda_function)) return 0;
    if (0 == a_try(self, a_number)) return 0;
    if (0 == a_try(self, a_string)) return 0;
    if (0 == a_try(self, a_char)) return 0;
    if (0 == a_try(self, a_bool)) return 0;
    if (0 == a_try(self, a_nil)) return 0;
    if (0 == a_try(self, a_null)) return 0;
    if (0 == a_try(self, a_attributed_identifier)) {

        ast_node *ident = self->result;
        assert(ast_node_is_symbol(ident));
        int is_none = str_eq(ident->symbol.name, S("None"));
        mangle_name(self, ident);

        // an identifier with type arguments in a value position can be used as
        // an operand of type predicates (e.g. opt :: Option[Int])
        ast_node_array type_args;
        if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;
        if (type_args.size) {
            // Check for arity qualifier: name[TypeArgs]/N (function reference with explicit type args)
            int is_fn_ref = 0;
            if (0 == a_try(self, a_arity_qualifier)) {
                u8 arity = (u8)self->result->i64.val;
                ast_node_name_replace(ident,
                                      mangle_str_for_arity(self->ast_arena, ident->symbol.name, arity));
                is_fn_ref = 1;
            }
            mangle_name(self, ident);
            ast_node *r = ast_node_create_nfa(self->ast_arena, ident, (ast_node_sized)sized_all(type_args),
                                              (ast_node_sized){0});
            r->named_application.is_function_reference = is_fn_ref;
            return result_ast_node(self, r);
        } else {
            // Syntax sugar: promote naked None or nullary tagged union variant to zero-arg call
            if (is_none || str_map_contains(self->nullary_variant_parents, ident->symbol.name)) {
                ast_node *r =
                  ast_node_create_nfa_tc(self->ast_arena, ident, (ast_node_sized){0}, (ast_node_sized){0});
                return result_ast_node(self, r);

            } else {
                return result_ast_node(self, ident);
            }
        }
    }
    // standalone attribute set is used as an operand of a type predicates
    if (0 == a_try(self, a_attribute_set)) return 0;

    self->error.tag = tl_err_expected_value;
    return 1;
}

int operator_precedence(char const *op, int is_prefix) {

    struct item {
        char const *op;
        int         p;
    };
    static struct item const infix[] = {

      {"=", 5},   {"+=", 5},   {"-=", 5},  {"*=", 5}, {"/=", 5}, {"%=", 5},

      {"<<=", 5}, {">>=", 5},  {"&=", 5},  {"^=", 5}, {"|=", 5},

      {"||", 10}, {"&&", 20},  {"|", 30},  {"&", 40},

      {"::", 50}, {"==", 50},  {"!=", 50},

      {"<", 60},  {"<=", 60},  {">=", 60}, {">", 60},

      {"<<", 70}, {">>", 70},

      {"+", 80},  {"-", 80},

      {"*", 90},  {"/", 90},   {"%", 90},

      {".", 110}, {"->", 110}, {"[", 110},

      {null, 0},
    };

    static struct item const prefix[] = {
      {"-", 100},
      {"+", 100},
      {"!", 100},
      {"~", 100},
      //
      {"*", 100},
      {"&", 100},
      //
      {null, 0},
    };

    for (struct item const *search = is_prefix ? prefix : infix; search->op; ++search) {
        if (0 == strcmp(op, search->op)) return search->p;
    }
    return INT_MIN;
}

int a_expression(parser *self) {
    ast_node *res = parse_expression(self, INT_MIN);
    if (!res) return 1;
    return result_ast_node(self, res);
}

int a_funcall(parser *self) {

    if (a_try(self, a_attributed_identifier)) return 1;
    ast_node      *name = self->result;

    ast_node_array type_args;
    if (ERROR_STOP == maybe_type_arguments(self, &type_args)) return ERROR_STOP;

    if (a_try(self, a_open_round)) return 1;

    ast_node_array args = {.alloc = self->ast_arena};
    if (0 == a_try(self, a_close_round)) goto done;
    if (0 == a_try(self, a_expression)) {
        ast_node *_t = maybe_wrap_lambda_function_in_let_in(self, self->result);
        array_push(args, _t);
    }

    while (1) {
        if (0 == a_try(self, a_close_round)) goto done;
        if (a_try(self, a_comma)) return 1;
        if (a_try(self, a_expression)) return 1;
        {
            ast_node *_t = maybe_wrap_lambda_function_in_let_in(self, self->result);
            array_push(args, _t);
        }
    }

done:
    array_shrink(args);

    // Function alias resolution: rewrite alias name to target function's name and module
    // BEFORE arity/variadic resolution, so that the target module's symbols are used.
    function_alias_info *alias = null;
    if (self->function_aliases && ast_node_is_symbol(name)) {
        alias = str_map_get_ptr(self->function_aliases, name->symbol.name);
        if (alias) {
            name->symbol.name   = str_copy(self->ast_arena, alias->base_name);
            name->symbol.module = str_copy(self->ast_arena, alias->module);
            parser_dbg(self, "function alias call '%s' -> '%s.%s'\n", str_cstr(&name->symbol.name),
                       str_cstr(&alias->module), str_cstr(&alias->base_name));
        }
    }

    // IMPORTANT: arity-mangle FIRST, then module-mangle.
    // symbol_is_module_function checks for the arity-mangled name (e.g., "foo__0") in
    // current_module_symbols. If we module-mangle first, we'd be checking for the wrong name.
    mangle_name_for_arity(self, name, args.size, 0); // 0 = function call, not definition

    // Check variadic_symbols: a variadic function accepts >= n_fixed_params args and has a fixed
    // mangled arity. We check using the original (pre-mangle) name, because arity-mangling may
    // have matched the function's fixed arity (n_fixed + 1) even for variadic calls.
    int is_variadic_call = 0;
    u8  n_fixed_args     = 0;
    if (self->variadic_symbols && ast_node_is_symbol(name)) {
        str lookup_name = str_is_empty(name->symbol.original) ? name->symbol.name : name->symbol.original;
        variadic_symbol_info *vinfo = str_map_get_ptr(self->variadic_symbols, lookup_name);
        if (vinfo && args.size >= vinfo->n_fixed_params) {
            // Use the variadic function's mangled name
            ast_node_name_replace(name, str_copy(self->ast_arena, vinfo->mangled_name));
            is_variadic_call = 1;
            n_fixed_args     = vinfo->n_fixed_params;
            parser_dbg(self, "variadic call '%s' -> '%s' (n_fixed=%d, n_args=%d)\n",
                       str_cstr(&name->symbol.original), str_cstr(&name->symbol.name), (int)n_fixed_args,
                       (int)args.size);
        }
    }

    // mangle_name uses current_module (empty for main), so aliases need explicit target module.
    if (alias) {
        mangle_name_for_module(self, name, alias->module);
    } else {
        mangle_name(self, name);
    }

    ast_node *node = ast_node_create_nfa(self->ast_arena, name, (ast_node_sized)sized_all(type_args),
                                         (ast_node_sized)sized_all(args));
    node->named_application.is_variadic_call = is_variadic_call;
    node->named_application.n_fixed_args     = n_fixed_args;
    return result_ast_node(self, node);
}

// Conditional variant binding: if c: Circle := shape { ... } [else { ... }]
// With else:    desugars to when shape { c: Circle { ... } else { ... } }
// Without else: desugars to single-arm case (statement, no value)
static int a_conditional_variant_binding(parser *self) {
    int res = a_try(self, a_param);
    if (res) return res;
    ast_node *lval = self->result;
    if (!ast_node_is_symbol(lval) || !lval->symbol.annotation) return 1;
    if (a_try(self, a_colon_equal)) return 1;

    ast_node *val = parse_expression(self, INT_MIN);
    if (!val) return 1;

    ast_node *yes = parse_body(self);
    if (!yes) return ERROR_STOP;

    ast_node_array conditions = {.alloc = self->ast_arena};
    ast_node_array arms       = {.alloc = self->ast_arena};
    array_push(conditions, lval);
    array_push(arms, yes);

    int is_union = AST_TAGGED_UNION_CONDITIONAL;

    if (0 == a_try_s(self, the_symbol, "else")) {
        ast_node *else_body = parse_body(self);
        if (!else_body) return ERROR_STOP;

        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_body);

        is_union = AST_TAGGED_UNION_VALUE;
    }

    ast_node *node =
      ast_node_create_case(self->ast_arena, val, (ast_node_sized)array_sized(conditions),
                           (ast_node_sized)array_sized(arms), null, null, is_union);
    set_node_file(self, node);
    return result_ast_node(self, node);
}

ast_node *parse_if_continue(parser *self) {
    if (0 == a_try(self, a_conditional_variant_binding)) return self->result;

    // the "if" token has been seen
    ast_node *cond = parse_expression(self, INT_MIN);
    if (!cond) return null;
    if (a_try(self, a_open_curly)) {
        self->error.tag = tl_err_expected_if_then_arm;
        return null;
    }

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};
    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return null;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    ast_node *yes = create_body(self, exprs, defers);
    exprs         = (ast_node_array){.alloc = self->ast_arena};
    defers        = (ast_node_array){.alloc = self->ast_arena};

    ast_node *no  = null;

    if (0 == a_try_s(self, the_symbol, "else")) {
        if (0 == a_try_s(self, the_symbol, "if")) {
            no = parse_if_continue(self);
        } else {
            if (a_try(self, a_open_curly)) return null;
            while (1) {
                if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
                else if (a_try(self, a_body_element)) return null;
                else array_push(exprs, self->result);
                if (0 == a_try(self, a_close_curly)) break;
            }
            no = create_body(self, exprs, defers);
        }
    }
    if (!no) no = null; // ok to have no case null

    ast_node *n = ast_node_create_if_then_else(self->ast_arena, cond, yes, no);
    set_node_file(self, n);
    return n;
}

ast_node *parse_if_expr(parser *self) {
    if (a_try_s(self, the_symbol, "if")) {
        self->error.tag = tl_err_ok;
        return null;
    }
    return parse_if_continue(self);
}

ast_node *parse_body(parser *self) {
    if (a_try(self, a_open_curly)) return null;

    ast_node_array exprs  = {.alloc = self->ast_arena};
    ast_node_array defers = {.alloc = self->ast_arena};

    // Check for empty body `{ }` as sugar for `{ void }`
    if (0 == a_try(self, a_close_curly)) {
        (void)result_ast(self, ast_void);
        array_push(exprs, self->result);
        return create_body(self, exprs, defers);
    }

    while (1) {
        if (0 == a_try(self, a_defer_statement)) array_push(defers, self->result);
        else if (a_try(self, a_body_element)) return null;
        else array_push(exprs, self->result);
        if (0 == a_try(self, a_close_curly)) break;
    }

    return create_body(self, exprs, defers);
}

//

ast_node *parse_case_expr(parser *self) {
    if (a_try_s(self, the_symbol, "case")) {
        self->error.tag = tl_err_ok;
        return null;
    }

    ast_node *expr = parse_expression(self, INT_MIN);
    if (!expr) return null;

    int is_pointer = 0;
    if (ast_node_is_unary_op(expr)) {
        if (is_ampersand(expr->unary_op.op)) is_pointer = 1;
        else return null; // no other unary op is valid

        // reset variable to the actual expression
        expr = expr->unary_op.operand;
    }

    // look for optional predicate
    ast_node *bin_pred = null;
    if (0 == a_try(self, a_comma)) {
        bin_pred = parse_expression(self, INT_MIN);
        if (!bin_pred) return null;
        if (!ast_node_is_symbol(bin_pred) && !ast_node_is_lambda_function(bin_pred)) return null;
    }

    // look for optional union type for destructure expression. Exclusive with predicate, however.
    ast_node *union_type   = null;
    str       union_module = str_empty();
    if (!bin_pred) {
        if (0 == a_try(self, a_type_annotation)) {
            // case s: Foo.Shape { c: Circle { ... } ... }
            // `Foo.Shape` will be mangled to symbol Foo_Shape by a_type_annotation

            union_type = self->result;
            // Must be a symbol: other type annotations e.g. arrows and nfas are not permitted.
            if (!ast_node_is_symbol(union_type)) goto begin_body;
            union_module = union_type->symbol.module;
        }
    }

begin_body:
    if (a_try(self, a_open_curly)) return null;

    ast_node_array conditions = {.alloc = self->ast_arena};
    ast_node_array arms       = {.alloc = self->ast_arena};
    ast_node      *else_arm   = null;
    while (1) {
        // check for else condition
        if (0 == a_try_s(self, the_symbol, "else")) {
            if (else_arm) {
                self->error.tag = tl_err_unexpected_else;
                return null;
            }
            else_arm = parse_body(self);
            if (!else_arm) return null;
            continue;
        }
        if (0 == a_try(self, a_close_curly)) break;

        ast_node *cond = null;
        if (union_type) {
            // a union case expression: condition must be an annotated symbol: a symbol to be bound, and the
            // desired variant type to be matched. The variant type must be mangled to the union_module.
            // E.g. `c: Circle` is the symbol `c` with the annotation `Circle`, which must be mangled to
            // `Foo.Circle`.
            if (a_try(self, a_param)) return null;
            cond = self->result;
            if (!ast_node_is_symbol(cond) || !cond->symbol.annotation ||
                !ast_node_is_symbol(cond->symbol.annotation))
                return null;

            // mangle symbol for the union's module
            mangle_name_for_module(self, cond->symbol.annotation, union_module);
        } else {
            // standard case expression
            cond = parse_expression(self, INT_MIN);
        }

        if (!cond) return null;
        ast_node *body = parse_body(self);
        if (!body) return null;

        array_push(conditions, cond);
        array_push(arms, body);
    }

    if (else_arm) {
        // else arm always goes at the end
        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_arm);
    }

    int union_flag = 0;
    if (is_pointer) union_flag = AST_TAGGED_UNION_MUTABLE;
    else if (union_type) union_flag = AST_TAGGED_UNION_VALUE;

    ast_node *node =
      ast_node_create_case(self->ast_arena, expr, (ast_node_sized)array_sized(conditions),
                           (ast_node_sized)array_sized(arms), bin_pred, union_type, union_flag);
    set_node_file(self, node);
    return node;
}

ast_node *parse_when_expr(parser *self) {
    if (a_try_s(self, the_symbol, "when")) {
        self->error.tag = tl_err_ok;
        return null;
    }

    ast_node *expr = parse_expression(self, INT_MIN);
    if (!expr) return null;

    int is_pointer = 0;
    if (ast_node_is_unary_op(expr)) {
        if (is_ampersand(expr->unary_op.op)) is_pointer = 1;
        else return null; // no other unary op is valid

        // reset variable to the actual expression
        expr = expr->unary_op.operand;
    }

    if (a_try(self, a_open_curly)) return null;

    ast_node_array conditions = {.alloc = self->ast_arena};
    ast_node_array arms       = {.alloc = self->ast_arena};
    ast_node      *else_arm   = null;
    while (1) {
        // check for else condition
        if (0 == a_try_s(self, the_symbol, "else")) {
            if (else_arm) {
                self->error.tag = tl_err_unexpected_else;
                return null;
            }
            else_arm = parse_body(self);
            if (!else_arm) return null;
            continue;
        }
        if (0 == a_try(self, a_close_curly)) break;

        ast_node *cond = null;

        // a union case expression: condition must be an annotated symbol: a symbol to be bound, and the
        // desired variant type to be matched. The variant type must be mangled to the union_module.
        // E.g. `c: Circle` is the symbol `c` with the annotation `Circle`, which must be mangled to
        // `Foo.Circle`.
        if (a_try(self, a_param)) return null;
        cond = self->result;
        if (!ast_node_is_symbol(cond) || !cond->symbol.annotation ||
            !ast_node_is_symbol(cond->symbol.annotation))
            return null;

        // Note: Do not module-mangle annotation symbol: inference will use the unmangled name within the
        // module scope of the tagged union.

        if (!cond) return null;
        ast_node *body = parse_body(self);
        if (!body) return null;

        array_push(conditions, cond);
        array_push(arms, body);
    }

    if (else_arm) {
        // else arm always goes at the end
        ast_node *sentinel = ast_node_create_nil(self->ast_arena);
        set_node_file(self, sentinel);
        array_push(conditions, sentinel);
        array_push(arms, else_arm);
    }

    int union_flag = 0;
    if (is_pointer) union_flag = AST_TAGGED_UNION_MUTABLE;
    else union_flag = AST_TAGGED_UNION_VALUE;

    ast_node *node = ast_node_create_case(self->ast_arena, expr, (ast_node_sized)array_sized(conditions),
                                          (ast_node_sized)array_sized(arms), null, null, union_flag);
    set_node_file(self, node);
    return node;
}

//

int maybe_mangle_binop(parser *self, ast_node *op, ast_node **inout, ast_node *right) {
    // Module alias resolution: replace leftmost alias with original module name
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout)) {
        str *original = str_map_get(self->module_aliases, (*inout)->symbol.name);
        if (original) (*inout)->symbol.name = *original;
    }

    // Implicit submodule visibility: in #module CommandLine, bare "Args" resolves
    // to "CommandLine.Args" if "CommandLine.Args" is a known module.
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        !str_is_empty(self->current_module) &&
        !str_hset_contains(self->modules_seen, (*inout)->symbol.name)) {
        str combined = str_cat_3(self->transient, self->current_module, S("."), (*inout)->symbol.name);
        if (str_hset_contains(self->modules_seen, combined)) {
            (*inout)->symbol.name = str_copy(self->ast_arena, combined);
        }
    }

    // Module-mangled symbol recovery: if a symbol like CommandLine was mangled to
    // CommandLine__CommandLine (because it's also a type in the current module), but
    // the original name is a module, unmangle so module resolution can proceed.
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        (*inout)->symbol.is_module_mangled && !str_is_empty((*inout)->symbol.original) &&
        str_hset_contains(self->modules_seen, (*inout)->symbol.original)) {
        unmangle_name(self, *inout);
    }

    // Nested module resolution: if left is a module and "left.right" is also a module,
    // synthesize a combined module reference (e.g., Foo.Bar) for the next dot iteration.
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        str_hset_contains(self->modules_seen, (*inout)->symbol.name) && ast_node_is_symbol(right)) {
        str combined = str_cat_3(self->ast_arena, (*inout)->symbol.name, S("."), right->symbol.name);
        if (str_hset_contains(self->modules_seen, combined)) {
            right->symbol.name = combined;
            *inout             = right;
            return 1;
        }
    }
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout) &&
        str_hset_contains(self->modules_seen, (*inout)->symbol.name)) {
        ast_node *to_mangle = null;
        u8        arity     = 0;
        if (ast_node_is_symbol(right)) to_mangle = right;
        else if (ast_node_is_nfa(right)) {
            to_mangle = right->named_application.name;
            arity     = right->named_application.n_arguments;
        }
        if (to_mangle) {
            unmangle_name(self, to_mangle);
            // When mangling a cross-module function call like ModuleOne.f(-1, 1),
            // we need to also mangle for arity. The arity mangling in a_funcall
            // doesn't know about the target module, so we do it here.
            // Skip arity mangling for type constructors — they use the bare name.
            str target_module = (*inout)->symbol.name;
            str original_name = to_mangle->symbol.name;
            int is_tc         = ast_node_is_nfa(right) && right->named_application.is_type_constructor;
            if (!is_tc) {
                str      mangled_name = mangle_str_for_arity(self->ast_arena, original_name, arity);
                hashmap *module_syms  = resolve_module_symbols(self, target_module);
                if (module_syms && str_hset_contains(module_syms, mangled_name)) {
                    to_mangle->symbol.name = mangled_name;
                }
                // Variadic fallback for cross-module calls: check variadic_symbols regardless
                // of normal arity lookup (same as a_funcall — arity may match n_fixed + 1).
                if (self->variadic_symbols && ast_node_is_nfa(right)) {
                    variadic_symbol_info *vinfo = str_map_get_ptr(self->variadic_symbols, original_name);
                    if (vinfo && str_eq(vinfo->module, target_module) && arity >= vinfo->n_fixed_params) {
                        to_mangle->symbol.name = str_copy(self->ast_arena, vinfo->mangled_name);
                        right->named_application.is_variadic_call = 1;
                        right->named_application.n_fixed_args     = vinfo->n_fixed_params;
                        parser_dbg(self,
                                   "variadic cross-module call '%s.%s' -> '%s' "
                                   "(n_fixed=%d, n_args=%d)\n",
                                   str_cstr(&target_module), str_cstr(&original_name),
                                   str_cstr(&vinfo->mangled_name), (int)vinfo->n_fixed_params, (int)arity);
                    }
                }
            }
            mangle_name_for_module(self, to_mangle, target_module);

            // Auto-invoke nullary tagged union variants: Opt.Empty → value, not fn ref
            if (to_mangle == right) { // bare symbol, not NFA
                ast_node *wrapped =
                  maybe_auto_invoke_nullary_variant(self, to_mangle, original_name, target_module);
                if (wrapped) {
                    *inout = wrapped;
                    return 1;
                }
            }

            *inout = right;
            return 1;
        }
    }
    // UFCS cross-module: (x.Mod).foo(a, b) → binary_op(".", x, nfa("Mod__foo", [a, b]))
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_binary_op_struct_access(*inout) &&
        ast_node_is_nfa(right) && ast_node_is_symbol((*inout)->binary_op.right) &&
        str_hset_contains(self->modules_seen, (*inout)->binary_op.right->symbol.name)) {
        unmangle_name(self, right->named_application.name);
        mangle_name_for_module(self, right->named_application.name, (*inout)->binary_op.right->symbol.name);
        *inout = ast_node_create_binary_op(self->ast_arena, op, (*inout)->binary_op.left, right);
        set_node_file(self, *inout);
        return 1;
    }
    // Check if left side is a type with nested types (dot syntax for nested structs / tagged union
    // variants)
    if ((0 == str_cmp_c(op->symbol.name, ".")) && ast_node_is_symbol(*inout)) {
        str parent_name = (*inout)->symbol.name;
        str module      = (*inout)->symbol.module;

        // Cross-module case: use original (unmangled) name
        if ((*inout)->symbol.is_module_mangled && !str_is_empty((*inout)->symbol.original))
            parent_name = (*inout)->symbol.original;

        if (str_hset_contains(self->nested_type_parents, parent_name)) {
            ast_node *to_mangle = null;
            if (ast_node_is_symbol(right)) to_mangle = right;
            else if (ast_node_is_nfa(right)) to_mangle = right->named_application.name;

            if (to_mangle) {
                // Build candidate child name and verify it exists in the correct module's symbols
                str child_name =
                  to_mangle->symbol.is_module_mangled && !str_is_empty(to_mangle->symbol.original)
                    ? to_mangle->symbol.original
                    : to_mangle->symbol.name;
                str candidate_name = str_qualify(self->ast_arena, parent_name, child_name);

                // Look up in the appropriate module's symbol table
                hashmap *syms = null;
                if (!str_is_empty(module)) syms = resolve_module_symbols(self, module);
                else if (!str_is_empty(self->current_module))
                    syms = resolve_module_symbols(self, self->current_module);

                int found = 0;
                if (syms) found = str_hset_contains(syms, candidate_name);
                // Same-module in main (no module prefix): check current_module_symbols
                if (!found && str_is_empty(module))
                    found = str_hset_contains(self->current_module_symbols, candidate_name);

                if (found) {
                    unmangle_name(self, to_mangle);
                    to_mangle->symbol.name = candidate_name;
                    if (!str_is_empty(module)) mangle_name_for_module(self, to_mangle, module);
                    else mangle_name(self, to_mangle);

                    // Auto-wrap: if parent is a tagged union, wrap the result so it
                    // returns the tagged union instead of the bare variant struct.
                    ast_node *wrapped =
                      maybe_wrap_variant_in_tagged_union(self, parent_name, child_name, module, right);
                    *inout = wrapped ? wrapped : right;
                    return 1;
                }
            }
        }
    }

    return 0;
}

ast_node *parse_base_expression(parser *self) {

    if (0 == a_try_s(self, the_symbol, "try")) {
        ast_node *operand = parse_expression(self, INT_MIN);
        if (!operand) return null;
        ast_node *n = ast_node_create_try(self->ast_arena, operand);
        set_node_file(self, n);
        return n;
    }

    if (0 == a_try_int(self, a_unary_operator, INT_MIN)) {
        ast_node *op   = self->result;
        int       prec = operator_precedence(str_cstr(&op->symbol.name), 1);
        ast_node *expr = parse_expression(self, prec);
        if (!expr) return null;
        ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, expr);
        set_node_file(self, unary);
        return unary;
    }

    // lambda function is identified by open round, so we need to parse it before nil and grouped
    // expressions.
    if (0 == a_try(self, a_lambda_funcall)) return self->result;
    if (0 == a_try(self, a_lambda_function)) return self->result;
    if (0 == a_try(self, a_nil)) return self->result; // parse () before (...)

    if (0 == a_try(self, a_open_round)) {

        ast_node *expr = null;

        // check for let-in expression before recursing
        int res;
        if (0 == (res = a_try(self, a_assignment))) expr = self->result;
        if (ERROR_STOP == res) {
            return null;
        }

        // if not let-in expression, check for body ({ ... }) before recursing
        if (!expr) expr = parse_body(self);

        if (!expr) expr = parse_expression(self, INT_MIN);
        if (a_try(self, a_close_round)) return null;
        return expr;
    }

    ast_node *node;
    node = parse_if_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    node = parse_case_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    node = parse_when_expr(self);
    if (node) return node;
    if (self->error.tag != tl_err_ok) return null;

    if (0 == a_try(self, a_value)) return self->result;
    return null;
}

ast_node *parse_expression(parser *self, int min_prec) {

    ast_node *left = parse_base_expression(self);
    if (!left) return null;

    int assoc = 1; // 1: left assoc, 0: right assoc

    while (1) {
        if (0 == a_try_int(self, a_binary_operator, min_prec)) {
            ast_node *op = self->result;

            // Note: special case: .* and .& are converted to unary_op for legacy reasons
            // Note: special case: .[ index ] is pointer/CArray indexing
            if (0 == str_cmp_c(op->symbol.name, ".")) {
                if (0 == a_try(self, a_open_square)) {
                    ast_node *index_expr = parse_expression(self, INT_MIN);
                    if (!index_expr) return null;
                    if (a_try(self, a_close_square)) return null;
                    ast_node *index_op = ast_node_create_sym_c(self->ast_arena, ".[");
                    ast_node *binop =
                      ast_node_create_binary_op(self->ast_arena, index_op, left, index_expr);
                    set_node_file(self, binop);
                    left = binop;
                    continue;
                } else if (0 == a_try(self, a_star)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    set_node_file(self, unary);
                    left = unary;
                    continue;
                } else if (0 == a_try(self, a_ampersand)) {
                    op              = self->result;
                    ast_node *unary = ast_node_create_unary_op(self->ast_arena, op, left);
                    set_node_file(self, unary);
                    left = unary;
                    continue;
                }
            }

            int prec = operator_precedence(str_cstr(&op->symbol.name), 0);
            assert(prec >= min_prec);

            // if opening an index expression, reset min prec to minimum
            if (0 == str_cmp_c(op->symbol.name, "[")) prec = INT_MIN;

            ast_node *right = parse_expression(self, prec + assoc);
            if (!right) return null;

            // Note: special case: mangle Module.foo and Module.bar() to simple expressions
            if (maybe_mangle_binop(self, op, &left, right)) continue;

            // Note: special case: unmangle right hand side symbol following struct access operator
            char const *op_c = str_cstr(&op->symbol.name);
            if (is_struct_access_operator(op_c)) {
                unmangle_name(self, right);
            }

            // Note: special case: [ as binary operator, need to close it with ] token
            if (0 == str_cmp_c(op->symbol.name, "["))
                if (a_try(self, a_close_square)) return null;

            // Note: special case: detect type predicate with binop ::
            ast_node *binop = null;
            if (0 == str_cmp_c(op->symbol.name, "::")) {
                binop = ast_node_create_type_predicate(self->ast_arena, left, right);
            } else {
                binop = ast_node_create_binary_op(self->ast_arena, op, left, right);
            }
            set_node_file(self, binop);
            left = binop;

        } else break;
    }

    return left;
}
