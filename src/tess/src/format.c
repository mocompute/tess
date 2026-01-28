#include "format.h"

#include <ctype.h>
#include <string.h>

#define INDENT_WIDTH 4

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Count net braces { minus } on a line, ignoring strings/chars/comments
static int count_net_braces(char const *line) {
    int net = 0;
    int in_str = 0, in_chr = 0;
    for (int i = 0; line[i]; i++) {
        if (in_str) {
            if (line[i] == '\\' && line[i + 1]) { i++; continue; }
            if (line[i] == '"') in_str = 0;
        } else if (in_chr) {
            if (line[i] == '\\' && line[i + 1]) { i++; continue; }
            if (line[i] == '\'') in_chr = 0;
        } else {
            if (line[i] == '"') in_str = 1;
            else if (line[i] == '\'') in_chr = 1;
            else if (line[i] == '/' && line[i + 1] == '/') break;
            else if (line[i] == '{') net++;
            else if (line[i] == '}') net--;
        }
    }
    return net;
}

// Check if trimmed line starts with a given prefix
static int starts_with(char const *s, char const *prefix) {
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

// Trim leading whitespace, return pointer into same buffer
static char const *ltrim(char const *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Trim trailing whitespace in-place
static void rtrim(char *s) {
    int len = (int)strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n'))
        len--;
    s[len] = '\0';
}

// ---------------------------------------------------------------------------
// Operator spacing
// ---------------------------------------------------------------------------

// Is c a character that suggests the next +/- is unary?
static int is_unary_context(char c) {
    return c == '(' || c == ',' || c == '=' || c == '{' || c == '[' ||
           c == ':' || c == '<' || c == '>' || c == '!' || c == '|' ||
           c == '&' || c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '%' || c == ';' || c == '~' || c == '^';
}

// Macros to wrap array ops that need lvalue struct
#define EMIT_CHAR(out, c) do { char _c = (c); array_push(out, _c); } while(0)

static void emit_spaced_op(char_array *out, char const *op) {
    if (out->size > 0 && out->v[out->size - 1] != ' ')
        EMIT_CHAR(*out, ' ');
    for (int i = 0; op[i]; i++)
        EMIT_CHAR(*out, op[i]);
    EMIT_CHAR(*out, ' ');
}

static char last_nonspace(char_array *out) {
    for (int i = (int)out->size - 1; i >= 0; i--) {
        if (out->v[i] != ' ') return out->v[i];
    }
    return '\0';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static char *normalize_ops(allocator *alloc, char const *line) {
    // If line starts with # directive, return as-is
    char const *trimmed = ltrim(line);
    if (trimmed[0] == '#') {
        return alloc_strdup(alloc, line);
    }

    int len = (int)strlen(line);
    char_array out = {.alloc = alloc};
    array_reserve(out, len + 64);

    int in_str = 0, in_chr = 0;

    for (int i = 0; i < len; i++) {
        char c = line[i];

        // Handle string literals
        if (in_str) {
            EMIT_CHAR(out, c);
            if (c == '\\' && i + 1 < len) { EMIT_CHAR(out, line[++i]); continue; }
            if (c == '"') in_str = 0;
            continue;
        }
        if (in_chr) {
            EMIT_CHAR(out, c);
            if (c == '\\' && i + 1 < len) { EMIT_CHAR(out, line[++i]); continue; }
            if (c == '\'') in_chr = 0;
            continue;
        }
        if (c == '"') { in_str = 1; EMIT_CHAR(out, c); continue; }
        if (c == '\'') { in_chr = 1; EMIT_CHAR(out, c); continue; }

        // Line comment — emit rest verbatim
        if (c == '/' && i + 1 < len && line[i + 1] == '/') {
            // Ensure space before comment if not at start
            if (out.size > 0 && out.v[out.size - 1] != ' ')
                EMIT_CHAR(out, ' ');
            for (; i < len; i++)
                EMIT_CHAR(out, line[i]);
            break;
        }

        char next = (i + 1 < len) ? line[i + 1] : '\0';

        // Two-character operators (order matters: check before single-char)
        if (c == ':' && next == '=') { emit_spaced_op(&out, ":="); i++; continue; }
        if (c == ':' && next == ':') { emit_spaced_op(&out, "::"); i++; continue; }
        if (c == '=' && next == '=') { emit_spaced_op(&out, "=="); i++; continue; }
        if (c == '!' && next == '=') { emit_spaced_op(&out, "!="); i++; continue; }
        if (c == '>' && next == '=') { emit_spaced_op(&out, ">="); i++; continue; }
        if (c == '<' && next == '=') { emit_spaced_op(&out, "<="); i++; continue; }
        if (c == '&' && next == '&') { emit_spaced_op(&out, "&&"); i++; continue; }
        if (c == '|' && next == '|') { emit_spaced_op(&out, "||"); i++; continue; }
        if (c == '-' && next == '>') {
            // Member access (ident->ident) vs return type arrow
            char prev = last_nonspace(&out);
            if (is_ident_char(prev) || prev == ')') {
                EMIT_CHAR(out, '-');
                EMIT_CHAR(out, '>');
            } else {
                emit_spaced_op(&out, "->");
            }
            i++;
            continue;
        }
        if (c == '>' && next == '>') { emit_spaced_op(&out, ">>"); i++; continue; }
        if (c == '<' && next == '<') { emit_spaced_op(&out, "<<"); i++; continue; }
        if (c == '+' && next == '=') { emit_spaced_op(&out, "+="); i++; continue; }
        if (c == '-' && next == '=') { emit_spaced_op(&out, "-="); i++; continue; }
        if (c == '*' && next == '=') { emit_spaced_op(&out, "*="); i++; continue; }
        if (c == '/' && next == '=') { emit_spaced_op(&out, "/="); i++; continue; }

        // Dot operators — no spacing
        if (c == '.') { EMIT_CHAR(out,c); continue; }

        // Single =
        if (c == '=') { emit_spaced_op(&out, "="); continue; }

        // Comma: space after, none before
        if (c == ',') {
            // Remove trailing spaces before comma
            while (out.size > 0 && out.v[out.size - 1] == ' ')
                out.size--;
            EMIT_CHAR(out,',');
            EMIT_CHAR(out,' ');
            continue;
        }

        // Colon: context-dependent
        if (c == ':') {
            // Just emit `: ` — colon in type annotations
            // But not if it's already followed by space or is part of ::
            EMIT_CHAR(out,':');
            if (next != ' ' && next != '\0' && next != ':' && next != '=')
                EMIT_CHAR(out,' ');
            continue;
        }

        // Binary +/-
        if (c == '+' || c == '-') {
            // Scientific notation: 1e-2, 1.5E+3
            if (out.size > 0 && (out.v[out.size - 1] == 'e' || out.v[out.size - 1] == 'E')) {
                int epos = (int)out.size - 1;
                if (epos > 0 && isdigit((unsigned char)out.v[epos - 1])) {
                    EMIT_CHAR(out, c);
                    continue;
                }
            }
            // Check for unary context
            char prev = last_nonspace(&out);
            if (prev == '\0' || is_unary_context(prev)) {
                // Unary — no space before
                EMIT_CHAR(out, c);
                continue;
            }
            char op[2] = {c, '\0'};
            emit_spaced_op(&out, op);
            continue;
        }

        // Binary *
        if (c == '*') {
            // After `.` it's a dereference
            if (out.size > 0 && out.v[out.size - 1] == '.') {
                EMIT_CHAR(out,c);
                continue;
            }
            char prev = last_nonspace(&out);
            if (prev == '\0' || is_unary_context(prev)) {
                // Pointer dereference / unary
                EMIT_CHAR(out,c);
                continue;
            }
            emit_spaced_op(&out, "*");
            continue;
        }

        // /  (not comment, not /=)
        if (c == '/') {
            // Check for arity syntax: ident/digit
            if (out.size > 0 && is_ident_char(out.v[out.size - 1]) && isdigit((unsigned char)next)) {
                EMIT_CHAR(out,c);
                continue;
            }
            emit_spaced_op(&out, "/");
            continue;
        }

        // %
        if (c == '%') { emit_spaced_op(&out, "%"); continue; }

        // < and > (single)
        if (c == '<') { emit_spaced_op(&out, "<"); continue; }
        if (c == '>') { emit_spaced_op(&out, ">"); continue; }

        // | (single, not ||)
        if (c == '|') {
            EMIT_CHAR(out,'|');
            if (next != '\0' && next != ' ')
                EMIT_CHAR(out,' ');
            continue;
        }

        // & (single, not &&) — address-of, no special spacing
        if (c == '&') {
            EMIT_CHAR(out,c);
            continue;
        }

        // Collapse multiple spaces into one (outside strings)
        if (c == ' ' || c == '\t') {
            if (out.size == 0 || out.v[out.size - 1] == ' ')
                continue;
            EMIT_CHAR(out,' ');
            continue;
        }

        EMIT_CHAR(out,c);
    }

    // Trim trailing spaces
    while (out.size > 0 && out.v[out.size - 1] == ' ')
        out.size--;

    EMIT_CHAR(out,'\0');
    char *result = alloc_strdup(alloc, out.v);
    array_free(out);
    return result;
}

// ---------------------------------------------------------------------------
// Multi-line token alignment
// ---------------------------------------------------------------------------

enum {
    ALIGN_COLON_VALUE, // value after `: ` (right-pads after colon)
    ALIGN_COLONEQ, // :=
    ALIGN_OPEN_PAREN, // ( at depth 0
    ALIGN_ARROW,  // ->
    ALIGN_EQ,     // = (standalone)
    ALIGN_OPEN_BRACE, // { inline body
    ALIGN_CLOSE_BRACE, // } inline body end
    ALIGN_COUNT
};

// Check if a line contains a return-type arrow (-> at paren depth 0, not member access)
static int has_arrow(char const *line) {
    int in_str = 0, in_chr = 0, paren = 0;
    int len = (int)strlen(line);
    for (int i = 0; i < len; i++) {
        char c = line[i];
        if (in_str) {
            if (c == '\\' && i + 1 < len) { i++; continue; }
            if (c == '"') in_str = 0;
            continue;
        }
        if (in_chr) {
            if (c == '\\' && i + 1 < len) { i++; continue; }
            if (c == '\'') in_chr = 0;
            continue;
        }
        if (c == '"') { in_str = 1; continue; }
        if (c == '\'') { in_chr = 1; continue; }
        if (c == '/' && i + 1 < len && line[i + 1] == '/') break;
        if (c == '(') { paren++; continue; }
        if (c == ')') { paren--; continue; }
        if (paren != 0) continue;
        if (c == '-' && i + 1 < len && line[i + 1] == '>') {
            char prev = (i > 0) ? line[i - 1] : '\0';
            if (is_ident_char(prev) || prev == ')') continue;
            return 1;
        }
    }
    return 0;
}

// Find the column of an alignment token in a line, at paren depth 0,
// outside strings/chars/comments. Returns -1 if not found.
static int find_align_token(char const *line, int token_type) {
    int in_str = 0, in_chr = 0, paren = 0, brace = 0;
    int len = (int)strlen(line);
    for (int i = 0; i < len; i++) {
        char c = line[i];
        if (in_str) {
            if (c == '\\' && i + 1 < len) { i++; continue; }
            if (c == '"') in_str = 0;
            continue;
        }
        if (in_chr) {
            if (c == '\\' && i + 1 < len) { i++; continue; }
            if (c == '\'') in_chr = 0;
            continue;
        }
        if (c == '"') { in_str = 1; continue; }
        if (c == '\'') { in_chr = 1; continue; }
        if (c == '/' && i + 1 < len && line[i + 1] == '/') break;

        if (c == '(') { paren++; }
        if (c == ')') { paren--; }
        if (c == '{') { brace++; }
        if (c == '}') { brace--; }

        if (token_type == ALIGN_OPEN_PAREN) {
            if (c == '(' && paren == 1 && brace == 0) {
                return i;
            }
            continue;
        }

        if (token_type == ALIGN_OPEN_BRACE) {
            if (c == '{' && brace == 1 && paren == 0) {
                return i;
            }
            continue;
        }

        if (token_type == ALIGN_CLOSE_BRACE) {
            // Find last } at brace depth 0
            if (c == '}' && brace == 0 && paren == 0) {
                return i;
            }
            continue;
        }

        if (paren != 0) continue;

        char next = (i + 1 < len) ? line[i + 1] : '\0';
        char prev = (i > 0) ? line[i - 1] : '\0';

        switch (token_type) {
        case ALIGN_COLON_VALUE:
            if (c == ':' && next != ':') {
                // Skip past spaces after colon to find the value
                int j = i + 1;
                while (j < len && line[j] == ' ') j++;
                // Exclude := (which after normalization is `: =` or `:=`)
                if (j < len && line[j] == '=') break;
                if (j < len) return j;
            }
            break;
        case ALIGN_ARROW:
            if (c == '-' && next == '>') {
                // Exclude member access: ident-> or )->
                if (i > 0 && (is_ident_char(prev) || prev == ')')) continue;
                return i;
            }
            break;
        case ALIGN_COLONEQ:
            if (c == ':' && next == '=') return i;
            break;
        case ALIGN_EQ:
            if (c == '=' && next != '=') {
                // Exclude :=, !=, >=, <=, +=, -=, *=, /=
                if (prev == ':' || prev == '!' || prev == '>' || prev == '<' ||
                    prev == '+' || prev == '-' || prev == '*' || prev == '/' ||
                    prev == '=')
                    continue;
                return i;
            }
            break;
        default:
            break;
        }
    }
    return -1;
}

// Count leading spaces
static int leading_spaces(char const *line) {
    int n = 0;
    while (line[n] == ' ') n++;
    return n;
}

// Returns 1 if the line (after indent) is a comment-only line
static int is_comment_line(char const *line) {
    char const *t = ltrim(line);
    return t[0] == '/' && t[1] == '/';
}

// Pad a line so the token at `col` moves to `target_col`.
// Inserts spaces into the whitespace region before the token.
static char *pad_to_column(allocator *alloc, char const *line, int col, int target_col) {
    int pad = target_col - col;
    if (pad <= 0) return NULL;
    int old_len = (int)strlen(line);
    int new_len = old_len + pad;
    char *new_line = alloc_malloc(alloc, new_len + 1);

    int ws_start = col;
    while (ws_start > 0 && line[ws_start - 1] == ' ') ws_start--;

    memcpy(new_line, line, ws_start);
    int orig_ws = col - ws_start;
    memset(new_line + ws_start, ' ', orig_ws + pad);
    memcpy(new_line + ws_start + orig_ws + pad, line + col, old_len - col + 1);

    return new_line;
}

// Right-pad: insert spaces just before position `col` to move it to `target_col`.
static char *rpad_before_token(allocator *alloc, char const *line, int col, int target_col) {
    int pad = target_col - col;
    if (pad <= 0) return NULL;
    int old_len = (int)strlen(line);
    int new_len = old_len + pad;
    char *new_line = alloc_malloc(alloc, new_len + 1);

    memcpy(new_line, line, col);
    memset(new_line + col, ' ', pad);
    memcpy(new_line + col + pad, line + col, old_len - col + 1);

    return new_line;
}

// Try to align a group of lines on a particular token type.
// Returns 1 if alignment was performed.
static int try_align_token(allocator *alloc, char **lines, int start, int end, int token_type) {
    int max_col = 0;
    int all_match = 1;

    // For paren/brace alignment, require all lines to have arrows
    if (token_type == ALIGN_OPEN_PAREN || token_type == ALIGN_OPEN_BRACE ||
        token_type == ALIGN_CLOSE_BRACE) {
        for (int i = start; i < end; i++) {
            if (is_comment_line(lines[i])) continue;
            if (lines[i][0] == '\0') continue;
            if (!has_arrow(lines[i])) { all_match = 0; break; }
        }
        if (!all_match) return 0;
    }

    // First pass: check all non-comment lines have the token
    for (int i = start; i < end; i++) {
        if (is_comment_line(lines[i])) continue;
        if (lines[i][0] == '\0') continue;
        int col = find_align_token(lines[i], token_type);
        if (col < 0) { all_match = 0; break; }
        if (col > max_col) max_col = col;
    }
    if (!all_match) return 0;

    // Second pass: pad each line
    for (int i = start; i < end; i++) {
        if (is_comment_line(lines[i])) continue;
        if (lines[i][0] == '\0') continue;
        int col = find_align_token(lines[i], token_type);
        if (col < 0 || col == max_col) continue;

        char *new_line;
        if (token_type == ALIGN_CLOSE_BRACE || token_type == ALIGN_COLON_VALUE) {
            new_line = rpad_before_token(alloc, lines[i], col, max_col);
        } else {
            new_line = pad_to_column(alloc, lines[i], col, max_col);
        }
        if (new_line) lines[i] = new_line;
    }
    return 1;
}

// Try to align consecutive sub-runs of lines that have the token.
// Unlike try_align_token, this doesn't require ALL lines to match.
static void align_subruns(allocator *alloc, char **lines, int start, int end, int token_type) {
    int i = start;
    while (i < end) {
        // Skip lines that don't have the token
        if (is_comment_line(lines[i]) || lines[i][0] == '\0' ||
            find_align_token(lines[i], token_type) < 0) {
            i++;
            continue;
        }
        // Found start of a sub-run
        int run_start = i;
        while (i < end && !is_comment_line(lines[i]) && lines[i][0] != '\0' &&
               find_align_token(lines[i], token_type) >= 0) {
            i++;
        }
        if (i - run_start >= 2) {
            try_align_token(alloc, lines, run_start, i, token_type);
        }
    }
}

// Align a group of consecutive same-indent lines.
// Apply all matching token types sequentially.
static void align_group(allocator *alloc, char **lines, int start, int end) {
    if (end - start < 2) return;
    for (int t = 0; t < ALIGN_COUNT; t++) {
        if (t == ALIGN_COLONEQ || t == ALIGN_COLON_VALUE || t == ALIGN_EQ) {
            align_subruns(alloc, lines, start, end, t);
        } else {
            try_align_token(alloc, lines, start, end, t);
        }
    }
}

// Run alignment pass over all output lines
static void align_pass(allocator *alloc, char **lines, int nlines) {
    int i = 0;
    while (i < nlines) {
        // Skip blank lines and directives
        if (lines[i][0] == '\0' || ltrim(lines[i])[0] == '#') { i++; continue; }

        int group_start = i;
        int group_indent = leading_spaces(lines[i]);

        while (i < nlines && lines[i][0] != '\0' &&
               ltrim(lines[i])[0] != '#' &&
               leading_spaces(lines[i]) == group_indent) {
            i++;
        }

        align_group(alloc, lines, group_start, i);
    }
}

// ---------------------------------------------------------------------------
// Main formatter
// ---------------------------------------------------------------------------

str tl_format(allocator *alloc, char const *data, u32 size, char const *filename) {
    (void)filename;

    if (size == 0) {
        return str_init(alloc, "\n");
    }

    // Phase 1: Split into lines
    // Count lines first
    int line_count = 1;
    for (u32 i = 0; i < size; i++) {
        if (data[i] == '\n') line_count++;
    }

    // Allocate line array (pointers into mutable copies)
    char **lines = alloc_calloc(alloc, line_count + 1, sizeof(char *));
    int nlines = 0;
    {
        int start = 0;
        for (int i = 0; i <= (int)size; i++) {
            if (i == (int)size || data[i] == '\n') {
                int len = i - start;
                char *ln = alloc_malloc(alloc, len + 1);
                memcpy(ln, data + start, len);
                ln[len] = '\0';
                rtrim(ln);
                lines[nlines++] = ln;
                start = i + 1;
            }
        }
    }

    // Phase 2-5: Process lines — collect into out_lines array
    int out_cap = nlines * 2 + 16;
    char **out_lines = alloc_calloc(alloc, out_cap, sizeof(char *));
    int out_count = 0;

    #define EMIT_LINE(s) do { \
        if (out_count >= out_cap) { \
            out_cap *= 2; \
            char **new_out = alloc_calloc(alloc, out_cap, sizeof(char *)); \
            memcpy(new_out, out_lines, out_count * sizeof(char *)); \
            out_lines = new_out; \
        } \
        out_lines[out_count++] = (s); \
    } while(0)

    int depth = 0;
    int in_c_block = 0;
    int consecutive_blanks = 0;
    int pipe_col = -1; // column of first | in a tagged union definition
    int prev_was_multiline = 0; // previous toplevel construct was multi-line
    int last_output_was_blank = 1; // start of file counts as "blank"

    for (int i = 0; i < nlines; i++) {
        char const *trimmed = ltrim(lines[i]);

        // Blank line
        if (trimmed[0] == '\0') {
            consecutive_blanks++;
            if (consecutive_blanks <= 2) {
                EMIT_LINE(alloc_strdup(alloc, ""));
                last_output_was_blank = 1;
            }
            continue;
        }
        consecutive_blanks = 0;

        // #ifc — enter C block
        if (!in_c_block && starts_with(trimmed, "#ifc")) {
            EMIT_LINE(alloc_strdup(alloc, trimmed));
            last_output_was_blank = 0;
            in_c_block = 1;
            continue;
        }

        // Inside C block — verbatim
        if (in_c_block) {
            if (starts_with(trimmed, "#endc")) {
                in_c_block = 0;
                EMIT_LINE(alloc_strdup(alloc, trimmed));
                last_output_was_blank = 0;
            } else {
                // Preserve original line (with original indentation)
                EMIT_LINE(alloc_strdup(alloc, lines[i]));
                last_output_was_blank = 0;
            }
            continue;
        }

        // #endc outside C block (shouldn't happen but handle gracefully)
        // #module, #import, #include — indent 0
        if (trimmed[0] == '#') {
            // Ensure blank line after a multi-line construct before a directive
            if (prev_was_multiline && !last_output_was_blank) {
                EMIT_LINE(alloc_strdup(alloc, ""));
                last_output_was_blank = 1;
            }
            prev_was_multiline = 0;
            EMIT_LINE(alloc_strdup(alloc, trimmed));
            last_output_was_blank = 0;
            continue;
        }

        // Check if line starts with }
        int prev_depth = depth;
        if (trimmed[0] == '}') {
            // Count leading } braces to determine how much to dedent
            depth--;
            if (depth < 0) depth = 0;
        }

        int indent = depth * INDENT_WIDTH;

        // Check for tagged union continuation line starting with |
        if (trimmed[0] == '|') {
            if (pipe_col > 0) {
                indent = pipe_col;
            } else {
                indent += INDENT_WIDTH * 2;
            }
        } else {
            // Reset pipe_col when we hit a non-pipe, non-blank line
            pipe_col = -1;
        }

        // Compute net braces to determine depth after this line
        int net = count_net_braces(trimmed);
        int depth_after;
        if (trimmed[0] == '}') {
            depth_after = depth + net + 1; // +1 because we pre-decremented
        } else {
            depth_after = depth + net;
        }
        if (depth_after < 0) depth_after = 0;

        // Blank line separation for toplevel multi-line constructs
        if (depth == 0 && prev_depth == 0) {
            // At toplevel: if previous construct was multi-line, ensure blank line
            if (prev_was_multiline && !last_output_was_blank) {
                EMIT_LINE(alloc_strdup(alloc, ""));
                last_output_was_blank = 1;
            }
            // If this line starts a multi-line construct, ensure blank line before it
            if (depth_after > 0 && !last_output_was_blank) {
                EMIT_LINE(alloc_strdup(alloc, ""));
                last_output_was_blank = 1;
            }
            prev_was_multiline = 0;
        }

        // Detect end of multi-line construct (depth returning to 0)
        if (depth_after == 0 && prev_depth > 0) {
            prev_was_multiline = 1;
        } else if (depth_after == 0 && depth == 0 && prev_depth == 0) {
            // Single-line at depth 0, don't set prev_was_multiline
        }

        // Build indented line
        // Normalize operators for code lines (not comment-only lines)
        char const *content;
        if (starts_with(trimmed, "//")) {
            content = trimmed;
        } else {
            content = normalize_ops(alloc, trimmed);
        }

        // Detect tagged union definition: "Name : | ..." or "Name(x) : | ..."
        // After normalization the pattern is ": | " somewhere in the line
        {
            char const *p = strstr(content, ": | ");
            if (p && pipe_col < 0) {
                // Column of | = indent + offset of | in content
                pipe_col = indent + (int)(p - content) + 2;
            }
        }

        int content_len = (int)strlen(content);
        char *full_line = alloc_malloc(alloc, indent + content_len + 1);
        memset(full_line, ' ', indent);
        memcpy(full_line + indent, content, content_len + 1);
        EMIT_LINE(full_line);
        last_output_was_blank = 0;

        // Update depth
        depth = depth_after;
    }

    #undef EMIT_LINE

    // Phase 6: Alignment pass
    align_pass(alloc, out_lines, out_count);

    // Phase 7: Concatenate into str_build
    str_build sb = str_build_init(alloc, size + 256);
    for (int i = 0; i < out_count; i++) {
        str_build_cat(&sb, str_init(alloc, out_lines[i]));
        str_build_cat(&sb, S("\n"));
    }

    str result = str_build_finish(&sb);

    // Ensure exactly one trailing newline
    size_t rlen = str_len(result);
    if (rlen == 0) {
        str_deinit(alloc, &result);
        return str_init(alloc, "\n");
    }

    // Strip trailing newlines then add exactly one
    char const *rbuf = str_buf(&result);
    int end = (int)rlen;
    while (end > 0 && rbuf[end - 1] == '\n') end--;
    if (end == 0) {
        str_deinit(alloc, &result);
        return str_init(alloc, "\n");
    }

    str trimmed_result = str_init_n(alloc, rbuf, end);
    str_deinit(alloc, &result);
    str final = str_cat_c(alloc, trimmed_result, "\n");
    str_deinit(alloc, &trimmed_result);
    return final;
}
