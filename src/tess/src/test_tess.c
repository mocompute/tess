#include "alloc.h"
#include "array.h"
#include "ast.h"
#include "dbg.h"
#include "parser.h"
#include "tokenizer.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int test_tess_token_string(void) {
    int        error = 0;

    allocator *alloc = default_allocator();

    error += strcmp("comma", token_tag_to_string(tok_comma)) == 0 ? 0 : 1;

    token tok;

    {
        token_init(&tok, tok_equal_sign);
        char *s = token_to_string(alloc, &tok);
        error += 0 == strcmp("(equal_sign)", s) ? 0 : 1;
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

    allocator  *alloc = default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, (char_csized){.v = input, .size = (u32)strlen(input)}, "");
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
        error += tl_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);

        // still eof
        error += 1 == tokenizer_next(t, &tok, &err) ? 0 : 1;
        if (error) return error;
        error += tl_err_eof == err.tag ? 0 : 1;
        token_deinit(alloc, &tok);
    }

    tokenizer_destroy(&t);

    return error;
}

static int test_tokenizer_string(void) {
    int         error = 0;

    char const *input = " \"abcdef\"  ";

    allocator  *alloc = default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, (char_csized){.v = input, .size = strlen(input)}, "");
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
    allocator  *alloc = default_allocator();
    tokenizer  *t     = tokenizer_create(alloc, (char_csized){.v = input, .size = strlen(input)}, "");
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

static int test_parser_node_to_string(void) {
    int        error = 0;

    allocator *alloc = leak_detector_create();
    if (!alloc) return error + 1;

    //
    {
        char const *input = "(a, b)";
        parser     *p     = parser_create_simple(alloc, input, strlen(input));
        if (null == p) return error + 1;

        if (parser_next(p)) {
            parser_report_errors(p);
            dbg("failed input = '%s'\n", input);
            return error + 1;
        }
        ast_node *node;
        parser_result(p, &node);

        error += ast_tuple == node->tag ? 0 : 1;

        char *str = ast_node_to_string(alloc, node);
        dbg("str 2 = %s\n", str);
        error += 0 == strcmp("(tuple ((symbol a 0 [null]) (symbol b 0 [null])) [null])", str) ? 0 : 1;
        alloc_free(alloc, str);
        parser_destroy(&p);
    }

    leak_detector_destroy(&alloc);

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
    T(test_parser_node_to_string);

    return error;
}
