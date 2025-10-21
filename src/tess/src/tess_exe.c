#include "alloc.h"
#include "array.h"
#include "file.h"
#include "infer.h"
#include "parser.h"
#include "str.h"
#include "transpile.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include "readline/history.h"
#include "readline/readline.h"

// -- embed externs --
extern char const *embed_std_tl;

typedef struct {
    allocator      *arena;

    char const     *argv0;
    char const     *out_path;

    c_string_carray words;

    int             verbose;
    int             verbose_parse;
    int             no_preamble;
    int             help;
} state;

noreturn void usage(int status, char const *argv0) {
    char const *progname = file_basename(argv0);

    printf("Usage: %s [-hv] [c | repl] [-o outpath] [path1 path2 ... pathn] \n", progname);
    puts("Commands:\n");
    printf("    c                      transpile input files to stdout\n");
    printf("    repl                   launch the repl\n");
    printf("\nOptions:\n");
    printf("    -h                     print usage\n");
    printf("    -no-preamble           do not include std.tl preamble\n");
    printf("    -o                     write output to path instead of stdout\n");
    printf("    -v                     verbose logging\n");
    printf("    --verbose-parse        produce large amount of parse progress output\n");
    exit(status);
}

void state_init(state *self) {
    alloc_zero(self);
    self->arena    = arena_create(default_allocator(), 4096);
    self->argv0    = null;
    self->out_path = null;
    self->words    = (c_string_carray){.alloc = self->arena};
    array_reserve(self->words, 32);
    self->verbose       = 0;
    self->verbose_parse = 0;
    self->no_preamble   = 0;
    self->help          = 0;
}

void state_deinit(state *self) {
    arena_destroy(default_allocator(), &self->arena);

    alloc_invalidate(self);
}

void state_gather_single_options(state *self, char *str) {
    u32 len = (u32)strlen(str);
    for (u32 i = 1; i < len; ++i) {
        switch (str[i]) {
        case 'h': self->help = 1; break;
        case 'v': self->verbose = 1; break;
        default:  break;
        }
    }
}

void state_gather_long_option(state *self, char *str) {
    if (0 == strcmp("--verbose-parse", str)) self->verbose_parse = 1;
    else if (0 == strcmp("--no-preamble", str)) self->no_preamble = 1;
}

void state_gather_options(state *self, int argc, char *argv[]) {

    self->argv0 = argv[0];

    for (int i = 1; i < argc; ++i) {
        if ('-' == argv[i][0]) {

            if (0 == strcmp("-o", argv[i])) {
                self->out_path = argv[++i];
                continue;
            } else if (0 == strncmp("--", argv[i], 2)) {
                state_gather_long_option(self, argv[i]);
            } else {
                state_gather_single_options(self, argv[i]);
            }

        } else {
            array_push(self->words, argv[i]);
        }
    }
}

void eval_print(char *in) {
    printf("you said: '%s'\n", in);
}

int repl(state *self) {
    (void)self;
    while (1) {
        char *line = readline("tess > ");

        if (!line) continue;

        eval_print(line);
        add_history(line);

        free(line);
    }

    return 0;
}

int compile(state *self) {
    if (self->words.size < 2) usage(1, self->argv0);

    int error = 0;

    // embed std_tl header
    char_array preamble     = {.alloc = default_allocator()};
    size_t     preamble_len = strlen(embed_std_tl);
    array_reserve(preamble, preamble_len);

    if (!self->no_preamble) array_push_many(preamble, embed_std_tl, preamble_len);

    parser *parser = parser_create(default_allocator(), (char_csized)sized_all(preamble),
                                   (c_string_csized){.v = &self->words.v[1], .size = self->words.size - 1});
    if (!parser) fatal("could not create parser");

    allocator     *nodes_alloc = arena_create(default_allocator(), 64 * 1024);
    ast_node_array nodes       = {.alloc = nodes_alloc};

    if (self->verbose_parse) {
        if (parser_parse_all_verbose(parser, &nodes)) {
            parser_report_errors(parser);
            ++error;
            goto cleanup_parser;
        }
    } else {
        if (parser_parse_all(parser, &nodes)) {
            parser_report_errors(parser);
            ++error;
            goto cleanup_parser;
        }
    }

    tl_infer       *infer        = tl_infer_create(default_allocator());
    tl_infer_result infer_result = {0};
    tl_infer_set_verbose(infer, self->verbose);
    if (tl_infer_run(infer, (ast_node_sized)sized_all(nodes), &infer_result)) {
        tl_infer_report_errors(infer);
        error++;
        goto cleanup_ti;
    }

    transpile_opts transpile_opts = {.infer_result = infer_result};
    transpile     *transpile      = transpile_create(default_allocator(), &transpile_opts);

    str_build      program_build;
    if (transpile_compile(transpile, &program_build)) goto cleanup_tp;

    str program = str_build_finish(&program_build);

    if (self->out_path) {
        FILE *f = fopen(self->out_path, "wb");
        if (!f) fatal("could not open output file: '%s'", self->out_path);

        fprintf(f, "%.*s", str_ilen(program), str_buf(&program));

        fclose(f);
    } else {
        printf("%.*s", str_ilen(program), str_buf(&program));
    }

    str_deinit(default_allocator(), &program);

cleanup_tp:
    transpile_destroy(default_allocator(), &transpile);

cleanup_ti:
    tl_infer_destroy(default_allocator(), &infer);

cleanup_parser:
    array_free(preamble);
    parser_destroy(&parser);
    array_free(nodes);
    arena_destroy(default_allocator(), &nodes_alloc);
    return error;
}

int main(int argc, char *argv[]) {

    int   result = 0;

    state self;
    state_init(&self);
    state_gather_options(&self, argc, argv);
    if (self.help) usage(0, argv[0]);
    if (self.words.size == 0) usage(0, argv[0]);

    if (0 == strcmp("c", self.words.v[0])) {
        result = compile(&self);
    }

    else if (0 == strcmp("repl", self.words.v[0])) {
        return repl(&self);
    }

    state_deinit(&self);
    return result;
}
