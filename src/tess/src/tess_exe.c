#include "alloc.h"
#include "array.h"
#include "file.h"
#include "infer.h"
#include "parser.h"
#include "str.h"
#include "transpile.h"
#include "types.h"

#include <ctype.h>
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
        char *line = readline("tl > ");

        if (!line) continue;

        eval_print(line);
        add_history(line);

        free(line);
    }

    return 0;
}

void read_import_lines(char_csized input, str_array *output) {
    u32         pos = 0, capture_start = 0;
    char const *data                                            = input.v;
    u32         size                                            = input.size;

    enum { start, noise, start_hash, in_hash, stop_hash } state = start;
    while (pos < size) {
        switch (state) {
        case start: {
            char c = data[pos++];
            if ('#' == c) state = start_hash;
            else if (isspace(c)) state = start;
            else state = noise;
        } break;
        case noise: {
            char c = data[pos++];
            if ('\n' == c) state = start;
        } break;
        case start_hash: {
            capture_start = pos;
            state         = in_hash;
        } break;
        case in_hash: {
            char c = data[pos++];
            if ('\n' == c) state = stop_hash;
        } break;
        case stop_hash: {
            str       command = str_init_n(output->alloc, &data[capture_start], pos - capture_start);
            str_array words   = {.alloc = output->alloc};
            str_parse_words(command, &words);
            if (words.size > 2 && str_eq(words.v[0], S("import"))) array_push(*output, words.v[1]);
            state = start;
        } break;
        }
    }

    if (stop_hash == state) {
        // catch command at end of file
        str       command = str_init_n(output->alloc, &data[capture_start], pos - capture_start);
        str_array words   = {.alloc = output->alloc};
        str_parse_words(command, &words);
        if (words.size > 2 && str_eq(words.v[0], S("import"))) array_push(*output, words.v[1]);
    }
}

static int is_quoted(str arg) {
    span s = str_span(&arg);
    if (s.buf[0] != '"' || s.buf[s.len - 1] != '"') return 0;
    return 1;
}

static str strip_quotes(allocator *alloc, str quoted) {
    // TODO: would be better to operate on spans so we don't needlessly copy strings
    if (!is_quoted(quoted)) return str_empty();

    span s = str_span(&quoted);
    s.buf++;
    s.len -= 2;
    return str_copy_span(alloc, s);
}

static void do_one_import(str path, str_array *imports) {
    char *data;
    u32   size;

    // path must be a quoted string, so detect and eliminate quotes
    path = strip_quotes(imports->alloc, path);
    if (str_is_empty(path)) return;

    file_read(imports->alloc, str_cstr(&path), &data, &size);

    str_array file_imports = {.alloc = imports->alloc};
    array_reserve(file_imports, 32);

    read_import_lines((char_csized){.size = size, .v = data}, &file_imports);
    forall(i, file_imports) {
        array_push(*imports, file_imports.v[i]);
        do_one_import(file_imports.v[i], imports);
    }
}

static void do_one_file(str path, str_array *imports) {
    char *data;
    u32   size;

    file_read(imports->alloc, str_cstr(&path), &data, &size);

    str_array file_imports = {.alloc = imports->alloc};
    array_reserve(file_imports, 32);

    read_import_lines((char_csized){.size = size, .v = data}, &file_imports);
    forall(i, file_imports) {
        array_push(*imports, file_imports.v[i]);
        do_one_import(file_imports.v[i], imports);
    }
}

static str_sized collect_imports(state *self, c_string_csized files) {
    allocator *alloc  = self->arena;

    char_array buffer = {.alloc = alloc};
    array_reserve(buffer, 64 * 1024);

    str_array imports = {.alloc = alloc};
    array_reserve(imports, 32);

    forall(i, files) {
        char *data;
        u32   size;
        file_read(alloc, files.v[i], &data, &size);

        do_one_file(str_init_static(files.v[i]), &imports);
    }

    return (str_sized)array_sized(imports);
}

static str_sized files_in_order(state *self, c_string_csized files) {
    str_sized imports        = collect_imports(self, files);

    str_array files_in_order = {.alloc = self->arena};
    array_reserve(files_in_order, imports.size + files.size);

    forall(i, imports) {
        str file = imports.v[i];

        if (!is_quoted(file)) continue;
        file = strip_quotes(self->arena, file);
        array_push(files_in_order, file);
    }

    forall(i, files) {
        str file = str_init(self->arena, files.v[i]);
        array_push(files_in_order, file);
    }

    return (str_sized)array_sized(files_in_order);
}

int compile(state *self) {
    if (self->words.size < 2) usage(1, self->argv0);

    int error = 0;

    // embed std_tl header
    char_array preamble     = {.alloc = default_allocator()};
    size_t     preamble_len = strlen(embed_std_tl);
    array_reserve(preamble, preamble_len);

    if (!self->no_preamble) array_push_many(preamble, embed_std_tl, preamble_len);

    c_string_csized paths = {.v = &self->words.v[1], .size = self->words.size - 1};

    parser         *parser =
      parser_create(default_allocator(), (char_csized)sized_all(preamble), files_in_order(self, paths));
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
