#include "manifest.h"
#include "file.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Trim leading and trailing whitespace, returning pointer and length.
static void trim(char const *start, char const *end, char const **out_start, u32 *out_len) {
    while (start < end && isspace((unsigned char)*start)) start++;
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    *out_start = start;
    *out_len   = (u32)(end - start);
}

// Check if a span exactly matches a string literal.
static int span_eq(char const *s, u32 len, char const *lit) {
    u32 lit_len = (u32)strlen(lit);
    return len == lit_len && 0 == memcmp(s, lit, lit_len);
}

// Check if a span starts with a string literal (and is longer).
static int span_starts_with(char const *s, u32 len, char const *prefix) {
    u32 plen = (u32)strlen(prefix);
    return len > plen && 0 == memcmp(s, prefix, plen);
}

// Check if a span contains a quote character.
static int contains_quote(char const *s, u32 len) {
    for (u32 i = 0; i < len; i++)
        if (s[i] == '"') return 1;
    return 0;
}

// Parse an array value: [a, b, c]. Expects value to start with '['.
// Returns 0 on success, 1 on error.
static int parse_array(allocator *alloc, char const *val, u32 val_len, str **out, u32 *out_count,
                       int line_num) {
    *out       = 0;
    *out_count = 0;

    if (val_len == 0 || val[0] != '[') {
        fprintf(stderr, "manifest:%d: expected '[' for array value\n", line_num);
        return 1;
    }

    // Find closing bracket
    char const *close = 0;
    for (u32 i = 0; i < val_len; i++) {
        if (val[i] == ']') {
            close = val + i;
            break;
        }
    }
    if (!close) {
        fprintf(stderr, "manifest:%d: missing closing ']'\n", line_num);
        return 1;
    }

    // Reject trailing non-whitespace after ']'
    for (char const *t = close + 1; t < val + val_len; t++) {
        if (!isspace((unsigned char)*t)) {
            fprintf(stderr, "manifest:%d: unexpected text after ']'\n", line_num);
            return 1;
        }
    }

    // Content between brackets
    char const *inner     = val + 1;
    char const *inner_end = close;

    // Trim inner whitespace
    while (inner < inner_end && isspace((unsigned char)*inner)) inner++;
    while (inner_end > inner && isspace((unsigned char)*(inner_end - 1))) inner_end--;

    // Empty array
    if (inner >= inner_end) return 0;

    // Count elements (commas + 1)
    u32 count = 1;
    for (char const *p = inner; p < inner_end; p++)
        if (*p == ',') count++;

    str        *arr        = alloc_malloc(alloc, count * sizeof(str));
    u32         idx        = 0;

    char const *elem_start = inner;
    for (char const *p = inner; p <= inner_end; p++) {
        if (p == inner_end || *p == ',') {
            char const *ts;
            u32         tl;
            trim(elem_start, p, &ts, &tl);

            if (tl == 0) {
                fprintf(stderr, "manifest:%d: empty array element\n", line_num);
                return 1;
            }
            if (contains_quote(ts, tl)) {
                fprintf(stderr, "manifest:%d: quotes not allowed in values\n", line_num);
                return 1;
            }
            // Check for spaces in element
            for (u32 j = 0; j < tl; j++) {
                if (isspace((unsigned char)ts[j])) {
                    fprintf(stderr, "manifest:%d: spaces not allowed in array elements\n", line_num);
                    return 1;
                }
            }
            arr[idx++] = str_init_n(alloc, ts, tl);
            elem_start = p + 1;
        }
    }

    *out       = arr;
    *out_count = idx;
    return 0;
}

// ---------------------------------------------------------------------------
// Section types
// ---------------------------------------------------------------------------

enum { SECTION_NONE, SECTION_PACKAGE, SECTION_DEPEND, SECTION_DEPEND_OPTIONAL, SECTION_UNKNOWN };

// ---------------------------------------------------------------------------
// Main parser
// ---------------------------------------------------------------------------

int tl_manifest_parse(allocator *alloc, char const *data, u32 data_len, tl_manifest *out) {
    memset(out, 0, sizeof(*out));
    out->package.name    = str_empty();
    out->package.version = str_empty();
    out->package.author  = str_empty();

    // Temporary dynamic arrays for dependencies
    str_array dep_names        = {.alloc = alloc};
    str_array dep_versions     = {.alloc = alloc};
    str_array dep_paths        = {.alloc = alloc};

    str_array opt_dep_names    = {.alloc = alloc};
    str_array opt_dep_versions = {.alloc = alloc};
    str_array opt_dep_paths    = {.alloc = alloc};

    int       section          = SECTION_NONE;

    // Current dependency being built (index into dep arrays, or -1)
    int         cur_dep     = -1;
    int         cur_dep_opt = 0; // is current dep optional?

    char const *p           = data;
    char const *end         = data + data_len;
    int         line_num    = 0;
    int         error       = 0;

    while (p < end) {
        line_num++;

        // Find end of line
        char const *line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        // Trim the line
        char const *ls;
        u32         ll;
        trim(p, line_end, &ls, &ll);

        // Advance past newline
        p = (line_end < end) ? line_end + 1 : end;

        // Skip blank lines and comments
        if (ll == 0) continue;
        if (ls[0] == '#') continue;

        // Section header: [section] or [section.subsection]
        if (ls[0] == '[') {
            if (ll < 2 || ls[ll - 1] != ']') {
                fprintf(stderr, "manifest:%d: malformed section header\n", line_num);
                error = 1;
                continue;
            }
            char const *sec_start = ls + 1;
            u32         sec_len   = ll - 2;

            if (span_eq(sec_start, sec_len, "package")) {
                section = SECTION_PACKAGE;
            } else if (span_starts_with(sec_start, sec_len, "depend-optional.")) {
                section      = SECTION_DEPEND_OPTIONAL;
                str dep_name = str_init_n(alloc, sec_start + 16, sec_len - 16);
                str empty    = str_empty();
                array_push(opt_dep_names, dep_name);
                array_push(opt_dep_versions, empty);
                array_push(opt_dep_paths, empty);
                cur_dep     = (int)opt_dep_names.size - 1;
                cur_dep_opt = 1;
            } else if (span_starts_with(sec_start, sec_len, "depend.")) {
                section      = SECTION_DEPEND;
                str dep_name = str_init_n(alloc, sec_start + 7, sec_len - 7);
                str empty    = str_empty();
                array_push(dep_names, dep_name);
                array_push(dep_versions, empty);
                array_push(dep_paths, empty);
                cur_dep     = (int)dep_names.size - 1;
                cur_dep_opt = 0;
            } else {
                fprintf(stderr, "manifest:%d: unknown section '[%.*s]'\n", line_num, (int)sec_len,
                        sec_start);
                section = SECTION_UNKNOWN;
            }
            continue;
        }

        // Key = value
        char const *eq = 0;
        for (u32 i = 0; i < ll; i++) {
            if (ls[i] == '=') {
                eq = ls + i;
                break;
            }
        }
        if (!eq) {
            fprintf(stderr, "manifest:%d: expected 'key = value'\n", line_num);
            error = 1;
            continue;
        }

        char const *key_s;
        u32         key_l;
        trim(ls, eq, &key_s, &key_l);

        char const *val_s;
        u32         val_l;
        trim(eq + 1, ls + ll, &val_s, &val_l);

        // Multi-line array: if value starts with '[' but has no ']',
        // continue reading lines until we find the closing bracket.
        if (val_l > 0 && val_s[0] == '[') {
            int has_close = 0;
            for (u32 i = 0; i < val_l; i++)
                if (val_s[i] == ']') {
                    has_close = 1;
                    break;
                }
            if (!has_close) {
                char const *close = 0;
                for (char const *s = val_s + val_l; s < end; s++) {
                    if (*s == ']') {
                        close = s;
                        break;
                    }
                }
                if (!close) {
                    fprintf(stderr, "manifest:%d: missing closing ']'\n", line_num);
                    error = 1;
                    continue;
                }
                // Count consumed newlines
                for (char const *s = line_end; s <= close; s++)
                    if (*s == '\n') line_num++;
                // Extend value to end of the line containing ']'
                char const *close_line = close;
                while (close_line < end && *close_line != '\n') close_line++;
                val_l = (u32)(close_line - val_s);
                // Advance p past the consumed lines
                p = (close_line < end) ? close_line + 1 : end;
            }
        }

        if (key_l == 0) {
            fprintf(stderr, "manifest:%d: empty key\n", line_num);
            error = 1;
            continue;
        }

        // Check for quotes in scalar values (arrays checked in parse_array)
        if (val_l > 0 && val_s[0] != '[' && contains_quote(val_s, val_l)) {
            fprintf(stderr, "manifest:%d: quotes not allowed in values\n", line_num);
            error = 1;
            continue;
        }

        if (section == SECTION_UNKNOWN) continue;

        if (section == SECTION_NONE) {
            fprintf(stderr, "manifest:%d: key outside of section\n", line_num);
            error = 1;
            continue;
        }

        if (section == SECTION_PACKAGE) {
            if (span_eq(key_s, key_l, "name")) {
                out->package.name = str_init_n(alloc, val_s, val_l);
            } else if (span_eq(key_s, key_l, "version")) {
                out->package.version = str_init_n(alloc, val_s, val_l);
            } else if (span_eq(key_s, key_l, "author")) {
                out->package.author = str_init_n(alloc, val_s, val_l);
            } else if (span_eq(key_s, key_l, "modules")) {
                if (parse_array(alloc, val_s, val_l, &out->package.modules, &out->package.module_count,
                                line_num))
                    error = 1;
            } else if (span_eq(key_s, key_l, "lib_path")) {
                if (parse_array(alloc, val_s, val_l, &out->package.lib_path, &out->package.lib_path_count,
                                line_num))
                    error = 1;
            } else {
                fprintf(stderr, "manifest:%d: unknown key '%.*s' in [package]\n", line_num, (int)key_l,
                        key_s);
            }
        } else if (section == SECTION_DEPEND || section == SECTION_DEPEND_OPTIONAL) {
            if (cur_dep < 0) continue;
            str_array *versions = cur_dep_opt ? &opt_dep_versions : &dep_versions;
            str_array *paths    = cur_dep_opt ? &opt_dep_paths : &dep_paths;

            if (span_eq(key_s, key_l, "version")) {
                versions->v[cur_dep] = str_init_n(alloc, val_s, val_l);
            } else if (span_eq(key_s, key_l, "path")) {
                paths->v[cur_dep] = str_init_n(alloc, val_s, val_l);
            } else {
                char const *sec_name = cur_dep_opt ? "depend-optional" : "depend";
                fprintf(stderr, "manifest:%d: unknown key '%.*s' in [%s]\n", line_num, (int)key_l, key_s,
                        sec_name);
            }
        }
    }

    // Validate required fields
    if (str_is_empty(out->package.name)) {
        fprintf(stderr, "manifest: missing required field 'name' in [package]\n");
        error = 1;
    }
    if (str_is_empty(out->package.version)) {
        fprintf(stderr, "manifest: missing required field 'version' in [package]\n");
        error = 1;
    }

    // Build dependency arrays
    if (dep_names.size > 0) {
        out->dep_count = dep_names.size;
        out->deps      = alloc_malloc(alloc, dep_names.size * sizeof(tl_manifest_dep));
        for (u32 i = 0; i < dep_names.size; i++) {
            out->deps[i].name    = dep_names.v[i];
            out->deps[i].version = dep_versions.v[i];
            out->deps[i].path    = dep_paths.v[i];
            if (str_is_empty(dep_versions.v[i])) {
                str name = dep_names.v[i];
                fprintf(stderr, "manifest: missing required field 'version' in [depend.%.*s]\n",
                        (int)str_len(name), str_buf(&name));
                error = 1;
            }
        }
    }

    if (opt_dep_names.size > 0) {
        out->optional_dep_count = opt_dep_names.size;
        out->optional_deps      = alloc_malloc(alloc, opt_dep_names.size * sizeof(tl_manifest_dep));
        for (u32 i = 0; i < opt_dep_names.size; i++) {
            out->optional_deps[i].name    = opt_dep_names.v[i];
            out->optional_deps[i].version = opt_dep_versions.v[i];
            out->optional_deps[i].path    = opt_dep_paths.v[i];
            if (str_is_empty(opt_dep_versions.v[i])) {
                str name = opt_dep_names.v[i];
                fprintf(stderr, "manifest: missing required field 'version' in [depend-optional.%.*s]\n",
                        (int)str_len(name), str_buf(&name));
                error = 1;
            }
        }
    }

    return error;
}

int tl_manifest_parse_file(allocator *alloc, char const *path, tl_manifest *out) {
    char *data = 0;
    u32   size = 0;
    file_read(alloc, path, &data, &size);
    if (!data) {
        memset(out, 0, sizeof(*out));
        fprintf(stderr, "manifest: cannot read '%s'\n", path);
        return 1;
    }
    return tl_manifest_parse(alloc, data, size, out);
}
