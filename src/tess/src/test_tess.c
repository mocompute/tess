#include "alloc.h"
#include "ast.h"
#include "parser.h"
#include "tokenizer.h"
#include "transpiler.h"
#include "vector.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
    int        error = 0;

    allocator *alloc = alloc_default_allocator();
    tokenizer *t     = tokenizer_alloc(alloc);

    {
        char const *input = "  (  )  ";
        if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

        token           tok;
        tokenizer_error err;

        // expect open_round
        error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_open_round == tok.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // expect close round
        error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_close_round == tok.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // expect eof
        error += 1 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tess_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // still eof
        error += 1 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tess_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        tokenizer_deinit(alloc, t);
    }

    tokenizer_dealloc(alloc, &t);

    return error;
}

static int test_tokenizer_string(void) {
    int        error = 0;

    allocator *alloc = alloc_default_allocator();
    tokenizer *t     = tokenizer_alloc(alloc);

    {
        char const *input = " \"abcdef\"  ";
        if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

        token           tok;
        tokenizer_error err;

        // expect string
        error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_string == tok.tag ? 0 : 1;
        error += 0 == strcmp("abcdef", tok.s) ? 0 : 1;
        token_deinit(alloc, &tok);

        tokenizer_deinit(alloc, t);
    }

    tokenizer_dealloc(alloc, &t);

    return error;
}

static int test_tokenizer_terminal_static_string(void) {
    // regression test for ASAN
    int        error = 0;

    allocator *alloc = alloc_default_allocator();
    tokenizer *t     = tokenizer_alloc(alloc);

    {
        char const *input = "-";
        if (tokenizer_init(alloc, t, input, strlen(input))) return error + 1;

        token           tok;
        tokenizer_error err;

        // expect string
        error += 0 == tokenizer_next(alloc, t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tok_symbol == tok.tag ? 0 : 1;
        error += 0 == strcmp("-", tok.s) ? 0 : 1;
        token_deinit(alloc, &tok);

        tokenizer_deinit(alloc, t);
    }

    tokenizer_dealloc(alloc, &t);

    return error;
}

static int test_parser_init(void) {
    int         error = 0;

    char const *input = "()";

    allocator  *alloc = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!alloc) return error + 1;

    ast_pool *pool = ast_pool_create(alloc);
    if (null == pool) return error + 1;

    parser *p = parser_alloc(alloc);
    if (parser_init(alloc, p, pool, input, strlen(input))) return error + 1;

    // can skip deinit/dealloc due to arena
    // parser_deinit(p);
    // parser_dealloc(alloc, &p);
    // ast_pool_destroy(alloc, &pool);

    alloc_arena_destroy(alloc_default_allocator(), &alloc);

    return error;
}

static int test_parser_basic(void) {
    int         error = 0;

    char const *input = "a";

    allocator  *alloc = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!alloc) return error + 1;

    ast_pool *pool = ast_pool_create(alloc);
    if (null == pool) return error + 1;

    parser *p = parser_alloc(alloc);
    if (parser_init(alloc, p, pool, input, strlen(input))) return error + 1;

    if (parser_next(p)) return error + 1;

    ast_node_h node_h;
    parser_result(p, &node_h);
    ast_node *node = ast_pool_at(pool, node_h);

    error += ast_symbol == node->tag ? 0 : 1;
    error += 0 == strcmp(mos_string_str(&node->symbol.name), "a") ? 0 : 1;

    parser_deinit(p);
    parser_dealloc(alloc, &p);
    ast_pool_destroy(alloc, &pool);

    alloc_arena_deinit(alloc);
    alloc_arena_dealloc(alloc_default_allocator(), &alloc);

    return error;
}

static int test_parser_expression(void) {
    int         error = 0;

    char const *input = "let x = 5 in x + 2";

    allocator  *alloc = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!alloc) return error + 1;

    ast_pool *pool = ast_pool_create(alloc);
    if (null == pool) return error + 1;

    parser *p = parser_create(alloc, pool, input, strlen(input));
    if (null == p) return error + 1;

    if (parser_next(p)) return error + 1;
    ast_node_h node_h;
    parser_result(p, &node_h);
    ast_node *node = ast_pool_at(pool, node_h);

    error += ast_let_in == node->tag ? 0 : 1;

    alloc_arena_destroy(alloc_default_allocator(), &alloc);

    return error;
}

static int test_parser_node_to_string(void) {
    int         error = 0;

    char const *input = "1 + 2";

    allocator  *alloc = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!alloc) return error + 1;

    ast_pool *pool = ast_pool_create(alloc);
    if (null == pool) return error + 1;

    {
        parser *p = parser_create(alloc, pool, input, strlen(input));
        if (null == p) return error + 1;

        if (parser_next(p)) return error + 1;
        ast_node_h node_h;
        parser_result(p, &node_h);
        ast_node *node = ast_pool_at(pool, node_h);

        error += ast_infix == node->tag ? 0 : 1;

        char buf[64];
        if (ast_node_to_string_buf(pool, node, buf, 64)) return error + 1;

        error += 0 == strcmp("(infix + (i64 1) (i64 2))", buf) ? 0 : 1;
    }

    //
    {
        input     = "(a, b)";
        parser *p = parser_create(alloc, pool, input, strlen(input));
        if (null == p) return error + 1;

        if (parser_next(p)) return error + 1;
        ast_node_h node_h;
        parser_result(p, &node_h);
        ast_node *node = ast_pool_at(pool, node_h);

        error += ast_tuple == node->tag ? 0 : 1;

        char buf[64];
        if (ast_node_to_string_buf(pool, node, buf, 64)) return error + 1;

        error += 0 == strcmp("(tuple (symbol a) (symbol b))", buf) ? 0 : 1;
    }

    alloc_arena_destroy(alloc_default_allocator(), &alloc);

    return error;
}

static int test_parse_all(void) {
    int        error     = 0;

    allocator *vec_alloc = alloc_default_allocator();
    allocator *ast_alloc = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!ast_alloc) return error + 1;

    ast_pool *pool = ast_pool_create(ast_alloc);
    if (null == pool) return error + 1;

    {
        char const *input = "a\nb\nc";

        parser     *p     = parser_create(ast_alloc, pool, input, strlen(input));
        if (null == p) return error + 1;

        vec_t nodes;
        if (vec_init(vec_alloc, &nodes, sizeof(ast_node_h), 1024)) return error + 1;
        if (parser_parse_all(vec_alloc, p, &nodes)) return error + 1;

        // TODO syntax check, e.g. input of "a\nb\nc" parses correctly but
        // is not a correct program, it is just 3 symbol nodes

        error += 3 == vec_size(&nodes) ? 0 : 1;
        vec_deinit(vec_alloc, &nodes);
    }

    return error;
}

static int test_parse_to_c(void) {
    int        error          = 0;

    allocator *vec_alloc      = alloc_default_allocator();
    allocator *compiler_alloc = alloc_default_allocator();
    allocator *ast_alloc      = alloc_arena_create(alloc_default_allocator(), 4096);
    if (!ast_alloc) return error + 1;

    ast_pool *pool = ast_pool_create(ast_alloc);
    if (null == pool) return error + 1;

    {
        char const *input = "let main () = \n"
                            "  std_dbg \"hello world!\"\n\n\n";

        parser     *p     = parser_create(ast_alloc, pool, input, strlen(input));
        if (null == p) return error + 1;

        vec_t nodes;
        if (vec_init(vec_alloc, &nodes, sizeof(ast_node_h), 1024)) return error + 1;
        if (parser_parse_all(vec_alloc, p, &nodes)) return error + 1;
        error += 1 == vec_size(&nodes) ? 0 : 1;
        if (error) return error;

        vec_t transpiler_output;
        if (vec_init(vec_alloc, &transpiler_output, 1, 1024)) return error + 1;

        transpiler *transpiler = transpiler_create(compiler_alloc, pool, &transpiler_output, vec_alloc);
        if (!transpiler) return error + 1;

        if (transpiler_compile(transpiler, &nodes)) return error + 1;

        // print out the output byte array: add string terminator
        if (vec_push_back_byte(vec_alloc, &transpiler_output, '\0')) return error + 1;

        printf("Output:\n%s\n", (char const *)vec_cbegin(&transpiler_output));

        transpiler_destroy(&transpiler);
        vec_deinit(vec_alloc, &nodes);
    }

    return 1;
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

    return error;
}
