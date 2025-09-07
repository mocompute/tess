#include "alloc.h"
#include "array.h"
#include "file.h"
#include "parser.h"
#include "syntax.h"
#include "transpiler.h"
#include "type_inference.h"
#include "type_registry.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>

#include "readline/history.h"
#include "readline/readline.h"

struct state {
    allocator     *arena;

    char const    *argv0;
    char const    *out_path;

    c_string_array words;

    bool           verbose;
    bool           verbose_parse;
    bool           help;
};

noreturn void usage(int status, char const *argv0) {
    char const *progname = file_basename(argv0);

    printf("Usage: %s [-hv] [c | repl] [-o outpath] [path1 path2 ... pathn] \n", progname);
    puts("Commands:\n");
    printf("    c                      transpile input files to stdout\n");
    printf("    repl                   launch the repl\n");
    printf("\nOptions:\n");
    printf("    -h                     print usage\n");
    printf("    -o                     write output to path instead of stdout\n");
    printf("    -v                     verbose logging\n");
    printf("    --verbose-parse        produce large amount of parse progress output\n");
    exit(status);
}

void state_init(struct state *self) {
    alloc_zero(self);
    self->arena = arena_create(default_allocator(), 4096);

    self->words = (c_string_array){.alloc = self->arena};
    array_reserve(self->words, 32);
}

void state_deinit(struct state *self) {
    arena_destroy(default_allocator(), &self->arena);

    alloc_invalidate(self);
}

void state_gather_single_options(struct state *self, char *str) {
    u32 len = (u32)strlen(str);
    for (u32 i = 1; i < len; ++i) {
        switch (str[i]) {
        case 'h': self->help = true; break;
        case 'v': self->verbose = true; break;
        default:  break;
        }
    }
}

void state_gather_long_option(struct state *self, char *str) {
    if (0 == strcmp("--verbose-parse", str)) self->verbose_parse = true;
}

void state_gather_options(struct state *self, int argc, char *argv[]) {

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
            array_push_val(self->words, argv[i]);
        }
    }
}

void eval_print(char *in) {
    printf("you said: '%s'\n", in);
}

int repl(struct state *self) {
    (void)self;
    while (true) {
        char *line = readline("tess > ");

        if (!line) continue;

        eval_print(line);
        add_history(line);

        free(line);
    }

    return 0;
}

int compile(struct state *self) {
    if (self->words.size < 2) usage(1, self->argv0);

    int        error = 0;

    char_array input = {.alloc = default_allocator()};
    array_reserve(input, 64 * 1024);

    {
        allocator *file_arena = arena_create(default_allocator(), 32 * 1024);

        for (u32 i = 1; i < self->words.size; ++i) {
            char  *buf;
            size_t size;
            file_read(file_arena, self->words.v[i], &buf, &size);

            if (size) {
                size_t new_cap = input.capacity;
                while (input.size + size + 2 >= new_cap) {
                    new_cap *= 2;
                    if (new_cap > UINT32_MAX) fatal("overflow: input files too large.");
                }

                if (new_cap > input.capacity) array_reserve(input, (u32)new_cap);

                array_copy(input, buf, size);
                input.v[input.size++] = '\n';
            }

            alloc_free(file_arena, buf);
        }

        arena_destroy(default_allocator(), &file_arena);

        array_push_val(input, '\0');
    }

    parser *parser = parser_create(default_allocator(), (char_cslice)slice_all(input));
    if (!parser) fatal("could not create parser");

    allocator     *nodes_alloc = arena_create(default_allocator(), 64 * 1024);
    ast_node_array nodes       = {.alloc = nodes_alloc};

    if (self->verbose_parse) {
        if (parser_parse_all_verbose(parser, &nodes)) {
            parser_report_errors(parser);
            return ++error;
        }
    } else {
        if (parser_parse_all(parser, &nodes)) {
            parser_report_errors(parser);
            return ++error;
        }
    }

    syntax_checker *syntax = syntax_checker_create(default_allocator(), (ast_node_slice)slice_all(nodes));

    if (syntax_checker_run(syntax)) {
        syntax_checker_report_errors(syntax);
        error = 1;
        goto cleanup_syntax;
    }

    type_registry *tr = syntax_checker_type_registry(syntax);

    ti_inferer    *ti = ti_inferer_create(default_allocator(), &nodes, tr);
    ti_inferer_set_verbose(ti, self->verbose);
    if (ti_inferer_run(ti)) {
        ti_inferer_report_errors(ti);
        error = 1;
        goto cleanup_ti;
    }

    allocator  *transpile_alloc   = arena_create(default_allocator(), 64 * 1024);
    char_array  transpiler_output = {.alloc = transpile_alloc};

    transpiler *transpiler        = transpiler_create(default_allocator(), &transpiler_output, tr);
    transpiler_set_verbose(transpiler, self->verbose);
    if (transpiler_compile(transpiler, nodes.v, nodes.size)) fatal("error while transpiling");

    if (self->out_path) {
        FILE *f = fopen(self->out_path, "wb");
        if (!f) fatal("could not open output file: '%s'", self->out_path);

        fprintf(f, "%s", transpiler_output.v);

        fclose(f);
    } else {
        puts(transpiler_output.v);
    }

    transpiler_destroy(&transpiler);
    arena_destroy(default_allocator(), &transpile_alloc);

cleanup_ti:
    ti_inferer_destroy(default_allocator(), &ti);

cleanup_syntax:
    syntax_checker_destroy(&syntax);
    parser_destroy(&parser);

    arena_destroy(default_allocator(), &nodes_alloc);
    array_free(input);

    return error;
}

int main(int argc, char *argv[]) {

    int          result = 0;

    struct state self;
    state_init(&self);
    state_gather_options(&self, argc, argv);
    if (self.help) usage(0, argv[0]);
    if (self.words.size == 0) usage(0, argv[0]);

    if (0 == strcmp("c", self.words.v[0])) {
        result = compile(&self);
    } else if (0 == strcmp("repl", self.words.v[0])) {
        return repl(&self);
    }

    state_deinit(&self);
    return result;
}
