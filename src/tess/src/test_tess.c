#include "alloc.h"
#include "ast.h"
#include "dbg.h"
#include "parser.h"
#include "syntax.h"
#include "tokenizer.h"
#include "transpiler.h"
#include "type_inference.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int compile_input(char const *input) {

    dbg("\n---------------------------\n");
    dbg("Compiling input string:\n\n%s\n\n", input);

    allocator *vec_alloc = alloc_leak_detector_create();
    allocator *ast_alloc = alloc_leak_detector_create();
    allocator *ti_alloc  = alloc_leak_detector_create();

    parser    *p         = parser_create(ast_alloc, input, strlen(input));

    vectora    nodes     = VECA(vec_alloc, ast_node *);
    veca_reserve(&nodes, 1024);

    if (parser_parse_all(p, &nodes)) return 1;

    allocator      *syntax_alloc = alloc_leak_detector_create();
    syntax_checker *syntax       = syntax_checker_create(syntax_alloc);

    // TODO syntax check, e.g. input of "a\nb\nc" parses correctly but
    // is not a correct program, it is just 3 symbol nodes

    if (syntax_checker_run(syntax, (ast_node **)veca_data(&nodes), veca_size(&nodes))) return 1;

    ti_inferer *ti = ti_inferer_create(ti_alloc, &nodes);

    ti_inferer_run(ti);

    struct ast_node_iterator iter = {0};
    while (veca_iter(&nodes, &iter.base)) {
        char *str = ast_node_to_string(alloc_default_allocator(), *iter.ptr);
        dbg("node: %s\n", str);
        alloc_free(alloc_default_allocator(), str);
    }

    dbg("constraints:\n");
    ti_inferer_dbg_constraints(ti);
    dbg("substitutions:\n");
    ti_inferer_dbg_substitutions(ti);

    ti_inferer_destroy(ti_alloc, &ti);

    syntax_checker_destroy(&syntax);
    alloc_leak_detector_destroy(&syntax_alloc);

    veca_deinit(&nodes);
    parser_destroy(&p);

    alloc_leak_detector_destroy(&ti_alloc);
    alloc_leak_detector_destroy(&ast_alloc);
    alloc_leak_detector_destroy(&vec_alloc);

    dbg("\n---------------------------\n\n");
    return 0;
}

static int test_tess_token_string(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();

    error += strcmp("comma", token_tag_to_string(tok_comma)) == 0 ? 0 : 1;

    token tok;

    {
        token_init(&tok, tok_equal_sign);
        char *s = token_to_string(alloc, &tok);
        error += 0 == strcmp("(equal_sign)", s) ? 0 : 1;
        alloc_free(alloc, s);
    }

    {
        token_init_v(&tok, tok_newline_indent, 4);
        char *s = token_to_string(alloc, &tok);
        error += 0 == strcmp("(newline_indent 4)", s) ? 0 : 1;
        alloc_free(alloc, s);
    }

    {
        error += 0 == token_init_s(alloc, &tok, tok_number, "123") ? 0 : 1;
        if (error) return error;

        char *s = token_to_string(alloc, &tok);

        error += 0 == strcmp("(number \"123\")", s) ? 0 : 1;
        alloc_free(alloc, s);

        token_deinit(alloc, &tok);
    }

    return error;
}

static int test_tokenizer_basic(void) {
    int         error = 0;

    char const *input = "  (  )  ";

    allocator  *alloc = alloc_default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, input, strlen(input));
    if (!t) return ++error;

    {

        token           tok;
        tokenizer_error err;

        // expect open_round
        error += 0 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_open_round == tok.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // expect close round
        error += 0 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_close_round == tok.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // expect eof
        error += 1 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tess_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // still eof
        error += 1 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tess_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);
    }

    tokenizer_destroy(&t);

    return error;
}

static int test_tokenizer_string(void) {
    int         error = 0;

    char const *input = " \"abcdef\"  ";

    allocator  *alloc = alloc_default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, input, strlen(input));
    if (!t) return ++error;

    {
        token           tok;
        tokenizer_error err;

        // expect string
        error += 0 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_string == tok.tag ? 0 : 1;
        error += 0 == strcmp("abcdef", tok.s) ? 0 : 1;
    }

    tokenizer_destroy(&t);

    return error;
}

static int test_tokenizer_terminal_static_string(void) {
    // regression test for ASAN
    int         error = 0;

    char const *input = "-";
    allocator  *alloc = alloc_default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, input, strlen(input));
    if (!t) return ++error;

    {
        token           tok;
        tokenizer_error err;

        // expect string
        error += 0 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_symbol == tok.tag ? 0 : 1;
        error += 0 == strcmp("-", tok.s) ? 0 : 1;
    }

    tokenizer_destroy(&t);

    return error;
}

static int test_parser_init(void) {
    int         error = 0;

    char const *input = "()";

    allocator  *alloc = alloc_leak_detector_create();
    if (!alloc) return error + 1;

    parser *p = parser_create(alloc, input, strlen(input));
    if (!p) return ++error;

    parser_destroy(&p);

    alloc_leak_detector_destroy(&alloc);
    return error;
}

static int test_parser_basic(void) {
    int         error = 0;

    char const *input = "a";

    allocator  *alloc = alloc_leak_detector_create();
    if (!alloc) return error + 1;

    parser *p = parser_create(alloc, input, strlen(input));
    if (!p) return error + 1;

    if (parser_next(p)) return error + 1;

    ast_node *node;
    parser_result(p, &node);

    error += ast_symbol == node->tag ? 0 : 1;
    error += 0 == strcmp(mos_string_str(&node->symbol.name), "a") ? 0 : 1;

    parser_destroy(&p);
    alloc_leak_detector_destroy(&alloc);

    return error;
}

static int test_parser_expression(void) {
    int         error = 0;

    char const *input = "let x = 5 in x + 2";

    allocator  *alloc = alloc_leak_detector_create();
    if (!alloc) return error + 1;

    parser *p = parser_create(alloc, input, strlen(input));
    if (null == p) return error + 1;

    if (parser_next(p)) return error + 1;
    ast_node *node;
    parser_result(p, &node);

    error += ast_let_in == node->tag ? 0 : 1;

    parser_destroy(&p);
    alloc_leak_detector_destroy(&alloc);

    return error;
}

static int test_parser_node_to_string(void) {
    int         error = 0;

    char const *input = "1 + 2";

    allocator  *alloc = alloc_leak_detector_create();
    if (!alloc) return error + 1;

    {
        parser *p = parser_create(alloc, input, strlen(input));
        if (null == p) return error + 1;

        if (parser_next(p)) return error + 1;
        ast_node *node;
        parser_result(p, &node);

        error += ast_infix == node->tag ? 0 : 1;

        char *str = ast_node_to_string(alloc, node);
        dbg("str 1 = %s\n", str);
        error += 0 == strcmp("(infix + (i64 1 [null]) (i64 2 [null]) [null])", str) ? 0 : 1;
        alloc_free(alloc, str);

        parser_destroy(&p);
    }

    //
    {
        input     = "(a, b)";
        parser *p = parser_create(alloc, input, strlen(input));
        if (null == p) return error + 1;

        if (parser_next(p)) return error + 1;
        ast_node *node;
        parser_result(p, &node);

        error += ast_tuple == node->tag ? 0 : 1;

        char *str = ast_node_to_string(alloc, node);
        dbg("str 2 = %s\n", str);
        error += 0 == strcmp("(tuple ((symbol a [null]) (symbol b [null])) [null])", str) ? 0 : 1;
        alloc_free(alloc, str);
        parser_destroy(&p);
    }

    alloc_leak_detector_destroy(&alloc);

    return error;
}

static int test_parse_all(void) {

    char const *input = "let a = 1 in\n"
                        "let b = 2 in\n"
                        "let a = b in\n"  // a = 2
                        "let b = a in\n"  // b = 2
                        "b           \n"; // value = 2

    return compile_input(input);
}

static int test_let_fun(void) {
    char const *input = "let add a b = a + b\n"
                        "let main () = add 1 2\n";
    return compile_input(input);
}

static int test_parse_to_c(void) {
    int        error = 0;

    allocator *alloc = alloc_leak_detector_create();

    {
        char const *input = "let main () = \n"
                            "  std_dbg \"hello world!\"";

        parser     *p     = parser_create(alloc, input, strlen(input));
        if (null == p) return error + 1;

        vectora nodes = VECA(alloc, ast_node *);
        veca_reserve(&nodes, 1024);

        if (parser_parse_all(p, &nodes)) return error + 1;
        error += 1 == veca_size(&nodes) ? 0 : 1;
        if (error) return error;

        vector transpiler_output = VEC(char);
        vec_reserve(alloc, &transpiler_output, 1024);

        transpiler *transpiler = transpiler_create(alloc, &transpiler_output, alloc);
        if (!transpiler) return error + 1;

        if (transpiler_compile(transpiler, (vector *)&nodes)) return error + 1;

        // print out the output byte array: add string terminator
        vec_push_back_byte(alloc, &transpiler_output, '\0');

        printf("Output:\n%s\n", (char const *)vec_data(&transpiler_output));

        vec_deinit(alloc, &transpiler_output);
        transpiler_destroy(&transpiler);
        veca_deinit(&nodes);

        parser_destroy(&p);
    }

    alloc_leak_detector_destroy(&alloc);

    return error;
}

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

int main(void) {
    int          error      = 0;
    int          this_error = 0;

    unsigned int seed       = (unsigned int)time(0);

    fprintf(stderr, "Seed = %u\n\n", seed);
    srand(seed);

    T(test_tess_token_string);
    T(test_tokenizer_basic);
    T(test_tokenizer_string);
    T(test_tokenizer_terminal_static_string);
    T(test_parser_init);
    T(test_parser_basic);
    T(test_parser_expression);
    T(test_parser_node_to_string);
    T(test_parse_all);
    T(test_parse_to_c);
    T(test_let_fun);

    return error;
}
