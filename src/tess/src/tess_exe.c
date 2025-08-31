#include "alloc.h"
#include "file.h"
#include "parser.h"
#include "syntax.h"
#include "transpiler.h"
#include "types.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

#include "readline/history.h"
#include "readline/readline.h"

struct state {
    allocator   *arena;

    char const  *argv0;
    char const  *out_path;

    char const **words;
    u32          n_words;
    u32          cap_words;

    bool         verbose;
    bool         help;
};

noreturn void usage(int status, char const *argv0) {
    char const *progname = file_basename(argv0);

    printf("Usage: %s [-hv] [c | repl] [-o outpath] [path1 path2 ... pathn] \n", progname);
    exit(status);
}

void state_init(struct state *self) {
    alloc_zero(self);
    self->arena     = alloc_arena_create(alloc_default_allocator(), 4096);

    self->cap_words = 32;
    self->words     = alloc_calloc(self->arena, self->cap_words, sizeof self->words[0]);
}

void state_deinit(struct state *self) {
    alloc_arena_destroy(alloc_default_allocator(), &self->arena);

    alloc_invalidate(self);
}

void state_gather_single_options(struct state *self, char *str) {
    u32 len = strlen(str);
    for (u32 i = 1; i < len; ++i) {
        switch (str[i]) {
        case 'h': self->help = true; break;
        case 'v': self->verbose = true; break;
        default:  break;
        }
    }
}

void state_gather_options(struct state *self, int argc, char *argv[]) {

    self->argv0 = argv[0];

    for (int i = 1; i < argc; ++i) {
        if ('-' == argv[i][0]) {

            if (0 == strcmp("-o", argv[i])) {
                self->out_path = argv[++i];
                continue;
            } else {
                state_gather_single_options(self, argv[i]);
            }

        } else {
            if (self->n_words == self->cap_words)
                alloc_resize(self->arena, &self->words, &self->cap_words, self->cap_words * 2);
            self->words[self->n_words++] = argv[i];
        }
    }
}

void eval_print(char *in) {
    printf("you said: '%s'\n", in);
}

int repl(struct state *self) {
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
    if (self->n_words < 2) usage(1, self->argv0);

    u32   cap_input = 64 * 1024;
    u32   n_input   = 0;
    char *input     = alloc_calloc(alloc_default_allocator(), 1, cap_input);

    {
        allocator *file_arena = alloc_arena_create(alloc_default_allocator(), 32 * 1024);

        for (u32 i = 1; i < self->n_words; ++i) {
            char  *buf;
            size_t size;
            file_read(file_arena, self->words[i], &buf, &size);

            if (size) {
                size_t new_cap = cap_input;
                while (n_input + size + 2 >= new_cap) {
                    new_cap *= 2;
                    if (new_cap > UINT32_MAX) fatal("overflow: input files too large.");
                }
                if (new_cap > cap_input)
                    alloc_resize(alloc_default_allocator(), &input, &cap_input, new_cap);

                strcpy(&input[n_input], buf);
                input[n_input + size] = '\n'; // overwrite \0
                n_input += size + 1;
            }

            alloc_free(file_arena, buf);
        }

        alloc_arena_destroy(alloc_default_allocator(), &file_arena);
    }

    parser *parser = parser_create(alloc_default_allocator(), input, n_input);
    if (!parser) fatal("could not create parser");

    allocator        *nodes_alloc = alloc_arena_create(alloc_default_allocator(), 64 * 1024);
    struct ast_node **nodes;
    u32               n_nodes = 0;

    if (parser_parse_all(parser, nodes_alloc, &nodes, &n_nodes)) fatal("error while parsing.");

    allocator  *transpile_alloc   = alloc_arena_create(alloc_default_allocator(), 64 * 1024);
    vector      transpiler_output = VEC(char);

    transpiler *transpiler =
      transpiler_create(alloc_default_allocator(), &transpiler_output, transpile_alloc);
    if (transpiler_compile(transpiler, nodes, n_nodes)) fatal("error while transpiling");

    if (self->out_path) {
        FILE *f = fopen(self->out_path, "wb");
        if (!f) fatal("could not open output file: '%s'", self->out_path);

        fprintf(f, "%s", (char *)vec_data(&transpiler_output));

        fclose(f);
    } else {
        puts(vec_data(&transpiler_output));
    }

    alloc_arena_destroy(alloc_default_allocator(), &transpile_alloc);
    alloc_arena_destroy(alloc_default_allocator(), &nodes_alloc);
    alloc_free(alloc_default_allocator(), input);

    return 0;
}

int main(int argc, char *argv[]) {

    struct state self;
    state_init(&self);
    state_gather_options(&self, argc, argv);
    if (self.help) usage(0, argv[0]);
    if (self.n_words == 0) usage(0, argv[0]);

    if (0 == strcmp("c", self.words[0])) {
        return compile(&self);
    } else if (0 == strcmp("repl", self.words[0])) {
        return repl(&self);
    }
}
