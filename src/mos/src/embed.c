#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef MOS_WINDOWS
#include <dirent.h>
#endif

static void make_c_identifier(char *dest, char const *src, size_t len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < len - 1; i++) {
        if (isalnum(src[i]) || src[i] == '_') {
            dest[j++] = src[i];
        } else if (src[i] == '.' || src[i] == '-' || src[i] == ' ') {
            dest[j++] = '_';
        }
    }
    dest[j] = '\0';

    // Ensure it doesn't start with a digit
    if (isdigit(dest[0])) {
        memmove(dest + 1, dest, strlen(dest) + 1);
        dest[0] = '_';
    }
}

void generate_c_string(FILE *out, char const *var_name, FILE *in) {
    fprintf(out, "// Original file: %s\n", var_name);
    fprintf(out, "char const *%s = \n", var_name);

    int c;
    int line_length = 0;
    fprintf(out, "    \"");

    while ((c = fgetc(in)) != EOF) {
        switch (c) {
        case '\n':
            fprintf(out, "\\n");
            fprintf(out, "\"\n    \"");
            line_length = 0;

            break;
        case '\r':
            fprintf(out, "\\r");
            line_length += 2;
            break;
        case '\t':
            fprintf(out, "\\t");
            line_length += 2;
            break;
        case '\\':
            fprintf(out, "\\\\");
            line_length += 2;
            break;
        case '"':
            fprintf(out, "\\\"");
            line_length += 2;
            break;
        default:
            if (c >= 0x20 && c < 0x7f) {
                fputc(c, out);
                line_length++;
            } else {
                fprintf(out, "\\x%02x", (unsigned char)c);
                line_length += 4;
            }
        }

        if (line_length > 78) {
            fprintf(out, "\"\n    \"");
            line_length = 0;
        }
    }

    fprintf(out, "\";\n\n");
}

// Process a single file
int process_file(FILE *out, char const *path, char const *filename) {
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "Warning: Cannot open file %s\n", path);
        return 1;
    }

    char var_name[256];
    make_c_identifier(var_name, filename, sizeof(var_name));

    // Add prefix to ensure uniqueness
    char full_var_name[512];
    snprintf(full_var_name, sizeof(full_var_name), "embed_%s", var_name);

    generate_c_string(out, full_var_name, in);
    fclose(in);

    return 0;
}

// Process directory
void process_directory(char const *progname, char const *dir_path, char const *output_file) {
    DIR *dir = opendir(dir_path);
    if (!dir) {
        fprintf(stderr, "%s: Error: Cannot open directory %s\n", progname, dir_path);
        exit(1);
    }

    FILE *out = fopen(output_file, "w");
    if (!out) {
        fprintf(stderr, "%s: Error: Cannot create output file %s\n", progname, output_file);
        closedir(dir);
        exit(1);
    }

    // Write header
    fprintf(out, "// Generated from directory: %s\n\n", dir_path);

    struct dirent *entry;
    size_t         file_count = 0;

    // First pass: collect files and generate content
    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and hidden files
        if (entry->d_name[0] == '.') continue;

        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
            printf("%s: Processing: %s\n", progname, entry->d_name);

            if (0 == process_file(out, filepath, entry->d_name)) {
                file_count++;
            }
        }
    }

    fclose(out);
    closedir(dir);

    printf("%s: Generated %s with %zu embedded files\n", progname, output_file, file_count);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <directory> <output.c>\n", argv[0]);
        fprintf(stderr, "Example: %s ./assets assets.c\n", argv[0]);
        return 1;
    }

    char *progname = strrchr(argv[0], '/');
    if (progname) progname++;
    else progname = argv[0];

    process_directory(progname, argv[1], argv[2]);
    return 0;
}
