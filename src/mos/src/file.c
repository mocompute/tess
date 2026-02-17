#include "file.h"
#include "platform.h"
#include "str.h"
#include "types.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef MOS_APPLE
#include <mach-o/dyld.h>
#endif

int file_exists(str filename) {
    FILE *f = fopen(str_cstr(&filename), "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

int file_is_directory(str path) {
#ifdef MOS_WINDOWS
    DWORD attrs = GetFileAttributesA(str_cstr(&path));
    if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat st;
    if (stat(str_cstr(&path), &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

void file_read(allocator *alloc, char const *filename, char **out, u32 *out_size) {

    *out      = null;
    *out_size = 0;

    FILE *f   = fopen(filename, "rb");
    if (!f) {
        perror("failed to open file");
        fprintf(stderr, "error: %s\n", filename);
        return;
    }

    if (fseek(f, 0, SEEK_END)) {
        perror("seek failed");
        fclose(f);
        return;
    }

    long size = ftell(f);
    if (size < 0) {
        perror("failed to read file position");
        fclose(f);
        return;
    }

    if (fseek(f, 0, SEEK_SET)) {
        perror("failed to rewind");
        fclose(f);
        return;
    }

    char *buf = alloc_malloc(alloc, (size_t)size);
    if (!buf) {
        size = 0;
        goto cleanup;
    }

    size_t const n = fread(buf, 1, (size_t)size, f);
    if (n != (size_t)size || n > UINT32_MAX) {
        perror("failed to read file");
        alloc_free(alloc, buf);
        buf = null;
        goto cleanup;
    }

cleanup:
    fclose(f);
    *out      = buf;
    *out_size = (u32)size;
}

char const *file_basename(char const *input) {
    char const *p1 = strrchr(input, '/');
    char const *p2 = strrchr(input, '\\');
    char const *p  = (p1 > p2) ? p1 : p2;
    return p ? p + 1 : input;
}

char *file_current_working_directory(span buf) {
    char *out;
#ifndef MOS_WINDOWS
    out = getcwd(buf.buf, buf.len);
#else
    if (buf.len > INT_MAX) fatal("overflow");
    out = _getcwd(buf.buf, (int)buf.len);
#endif
    return out;
}

char *file_exe_directory(span buf) {
#ifdef MOS_WINDOWS
    DWORD len = GetModuleFileNameA(NULL, buf.buf, (DWORD)buf.len);
    if (len == 0 || len >= buf.len) return NULL;
#elif defined(MOS_APPLE)
    uint32_t size = (uint32_t)buf.len;
    if (_NSGetExecutablePath(buf.buf, &size) != 0) return NULL;
#else
    ssize_t len = readlink("/proc/self/exe", buf.buf, buf.len - 1);
    if (len == -1) return NULL;
    buf.buf[len] = '\0';
#endif
    // Find last path separator and truncate to get directory
    char *last_sep = strrchr(buf.buf, '/');
#ifdef MOS_WINDOWS
    char *last_sep_win = strrchr(buf.buf, '\\');
    if (last_sep_win > last_sep) last_sep = last_sep_win;
#endif
    if (last_sep) *last_sep = '\0';
    return buf.buf;
}

// -- path utilities --

str file_dirname(allocator *alloc, str path) {
    span s = str_span(&path);
    if (s.len == 0) return str_empty();

    // Find last separator
    char const *p = null;
    for (size_t i = s.len; i > 0; i--) {
        if (s.buf[i - 1] == '/' || s.buf[i - 1] == '\\') {
            p = s.buf + i - 1;
            break;
        }
    }

    // No directory separator means the file is in the current directory
    if (!p) return str_init(alloc, ".");

    size_t len = (size_t)(p - s.buf);
    if (len == 0) {
        // Root directory case: "/" or "\"
        return str_init_n(alloc, s.buf, 1);
    }

    return str_init_n(alloc, s.buf, len);
}

int file_is_absolute(str path) {
    span s = str_span(&path);
    if (s.len == 0) return 0;

    // Unix absolute: starts with /
    if (s.buf[0] == '/') return 1;

#ifdef MOS_WINDOWS
    // Windows absolute: starts with \ or drive letter (C:\)
    if (s.buf[0] == '\\') return 1;
    if (s.len >= 3 && s.buf[1] == ':' && (s.buf[2] == '/' || s.buf[2] == '\\')) return 1;
#endif

    return 0;
}

str file_path_join(allocator *alloc, str dir, str file) {
    if (str_is_empty(dir)) return str_copy(alloc, file);
    if (str_is_empty(file)) return str_copy(alloc, dir);

    // Check if dir already ends with separator
    span dir_span = str_span(&dir);
    char last     = dir_span.buf[dir_span.len - 1];
    int  has_sep  = (last == '/' || last == '\\');

    if (has_sep) {
        return str_cat(alloc, dir, file);
    } else {
        return str_cat_3(alloc, dir, S("/"), file);
    }
}

str file_path_normalize(allocator *alloc, str path) {
    span s = str_span(&path);
    if (s.len == 0) return str_empty();

    // Parse path into components, resolving . and ..
    // We use a simple stack-based approach

    int         is_abs     = file_is_absolute(path);
    char const *start      = s.buf;
    size_t      prefix_len = 0;

    // Handle absolute path prefixes
    if (is_abs) {
#ifdef MOS_WINDOWS
        if (s.buf[1] == ':') {
            prefix_len = 3; // "C:/"
        } else {
            prefix_len = 1; // "/" or "\"
        }
#else
        prefix_len = 1; // "/"
#endif
        start = s.buf + prefix_len;
    }

    // Stack of component start/length pairs
    // Maximum depth is s.len/2 (alternating char/separator)
    size_t      max_components = s.len / 2 + 1;
    size_t     *comp_start     = alloc_malloc(alloc, max_components * sizeof(size_t));
    size_t     *comp_len       = alloc_malloc(alloc, max_components * sizeof(size_t));
    size_t      comp_count     = 0;
    int         depth          = 0; // Track depth for relative paths

    char const *p              = start;
    char const *end            = s.buf + s.len;

    while (p < end) {
        // Skip separators
        while (p < end && (*p == '/' || *p == '\\')) p++;
        if (p >= end) break;

        // Find end of component
        char const *comp_begin = p;
        while (p < end && *p != '/' && *p != '\\') p++;
        size_t clen = (size_t)(p - comp_begin);

        // Handle . and ..
        if (clen == 1 && comp_begin[0] == '.') {
            // Current directory, skip
            continue;
        } else if (clen == 2 && comp_begin[0] == '.' && comp_begin[1] == '.') {
            // Parent directory
            if (comp_count > 0 && depth > 0) {
                // Pop the last component
                comp_count--;
                depth--;
            } else if (!is_abs) {
                // For relative paths, we need to track leading ..
                // Check if we're trying to go above root
                if (depth < 0) {
                    // Already have leading .., add another
                    comp_start[comp_count] = (size_t)(comp_begin - s.buf);
                    comp_len[comp_count]   = clen;
                    comp_count++;
                } else if (comp_count == 0) {
                    // No components to pop, add leading ..
                    comp_start[comp_count] = (size_t)(comp_begin - s.buf);
                    comp_len[comp_count]   = clen;
                    comp_count++;
                    depth = -1; // Mark that we have leading ..
                } else {
                    // Pop the last component
                    comp_count--;
                    depth--;
                }
            }
            // For absolute paths, just ignore .. at root
        } else {
            // Regular component
            comp_start[comp_count] = (size_t)(comp_begin - s.buf);
            comp_len[comp_count]   = clen;
            comp_count++;
            if (depth >= 0) depth++;
        }
    }

    // Build result
    str_build build = str_build_init(alloc, (u32)(s.len + 1));

    // Add prefix for absolute paths
    if (is_abs) {
        str_build_cat(&build, str_init_n(alloc, s.buf, prefix_len));
    }

    // Add components
    for (size_t i = 0; i < comp_count; i++) {
        if (i > 0 ||
            (is_abs && prefix_len > 0 && s.buf[prefix_len - 1] != '/' && s.buf[prefix_len - 1] != '\\')) {
            str_build_cat(&build, S("/"));
        }
        str_build_cat(&build, str_init_n(alloc, s.buf + comp_start[i], comp_len[i]));
    }

    // Handle empty result
    if (build.size == 0) {
        if (is_abs) {
            str_build_cat(&build, S("/"));
        } else {
            str_build_cat(&build, S("."));
        }
    }

    alloc_free(alloc, comp_start);
    alloc_free(alloc, comp_len);

    return str_build_finish(&build);
}

// Compute relative path from a directory to a target path.
//
// Given a starting directory (from_dir) and a target path (to_path), returns
// the relative path that would navigate from from_dir to to_path.
//
// If either path is relative, it is first converted to absolute using the
// current working directory.
//
// Examples:
//   file_path_relative("/a/b", "/a/b/file.txt")     -> "file.txt"
//   file_path_relative("/a/b", "/a/c/file.txt")     -> "../c/file.txt"
//   file_path_relative("/a/b/c", "/a/file.txt")     -> "../../file.txt"
//   file_path_relative("/a/b", "/a/b")              -> "."
//   file_path_relative("src", "src/file.c")         -> "file.c" (relative inputs OK)
//
// Returns empty string on error:
//   - Empty input paths
//   - Different drives on Windows (e.g., C: vs D:)
//   - Failed to get current working directory (for relative inputs)
str file_path_relative(allocator *alloc, str from_dir, str to_path) {
    if (str_is_empty(from_dir) || str_is_empty(to_path)) {
        return str_empty();
    }

    // Normalize both paths
    str from_norm = file_path_normalize(alloc, from_dir);
    str to_norm   = file_path_normalize(alloc, to_path);

    if (str_is_empty(from_norm) || str_is_empty(to_norm)) {
        str_deinit(alloc, &from_norm);
        str_deinit(alloc, &to_norm);
        return str_empty();
    }

    // If paths are relative, convert to absolute using cwd
    int from_abs = file_is_absolute(from_norm);
    int to_abs   = file_is_absolute(to_norm);

    if (!from_abs || !to_abs) {
        char cwd_buf[4096];
        if (!file_current_working_directory((span){.buf = cwd_buf, .len = sizeof(cwd_buf)})) {
            str_deinit(alloc, &from_norm);
            str_deinit(alloc, &to_norm);
            return str_empty();
        }
        str cwd = str_init_static(cwd_buf);

        if (!from_abs) {
            str abs_from = file_path_join(alloc, cwd, from_norm);

            str_deinit(alloc, &from_norm);
            from_norm = file_path_normalize(alloc, abs_from);
            str_deinit(alloc, &abs_from);
        }
        if (!to_abs) {
            str abs_to = file_path_join(alloc, cwd, to_norm);

            str_deinit(alloc, &to_norm);
            to_norm = file_path_normalize(alloc, abs_to);
            str_deinit(alloc, &abs_to);
        }

        if (str_is_empty(from_norm) || str_is_empty(to_norm)) {
            str_deinit(alloc, &from_norm);
            str_deinit(alloc, &to_norm);
            return str_empty();
        }
    }

#ifdef MOS_WINDOWS
    // On Windows, check that both paths are on the same drive
    char const *from_cstr = str_cstr(&from_norm);
    char const *to_cstr   = str_cstr(&to_norm);
    if (from_cstr[1] == ':' && to_cstr[1] == ':') {
        char from_drive = from_cstr[0];
        char to_drive   = to_cstr[0];
        // Normalize drive letter case
        if (from_drive >= 'a' && from_drive <= 'z') from_drive -= 32;
        if (to_drive >= 'a' && to_drive <= 'z') to_drive -= 32;
        if (from_drive != to_drive) {
            str_deinit(alloc, &from_norm);
            str_deinit(alloc, &to_norm);
            return str_empty(); // Different drives
        }
    }
#endif

    // Split paths into components
    span from_span = str_span(&from_norm);
    span to_span   = str_span(&to_norm);

    // Find common prefix length (by path components)
    size_t from_pos = 0;
    size_t to_pos   = 0;
    size_t last_sep = 0;

    // Skip initial path prefix (/ or C:/)
    if (from_span.len > 0 && (from_span.buf[0] == '/' || from_span.buf[0] == '\\')) {
        from_pos = 1;
    }
#ifdef MOS_WINDOWS
    else if (from_span.len >= 3 && from_span.buf[1] == ':') {
        from_pos = 3;
    }
#endif

    if (to_span.len > 0 && (to_span.buf[0] == '/' || to_span.buf[0] == '\\')) {
        to_pos = 1;
    }
#ifdef MOS_WINDOWS
    else if (to_span.len >= 3 && to_span.buf[1] == ':') {
        to_pos = 3;
    }
#endif

    // Find where paths diverge
    while (from_pos < from_span.len && to_pos < to_span.len) {
        char fc = from_span.buf[from_pos];
        char tc = to_span.buf[to_pos];

        // Normalize separators for comparison
        if (fc == '\\') fc = '/';
        if (tc == '\\') tc = '/';

        if (fc != tc) break;

        if (fc == '/') {
            last_sep = from_pos;
        }

        from_pos++;
        to_pos++;
    }

    // Check if we stopped at a separator or end of one path
    if (from_pos == from_span.len || to_pos == to_span.len) {
        char fc = (from_pos < from_span.len) ? from_span.buf[from_pos] : '/';
        char tc = (to_pos < to_span.len) ? to_span.buf[to_pos] : '/';
        if (fc == '/' || fc == '\\') last_sep = from_pos;
        if (tc == '/' || tc == '\\') last_sep = from_pos;
        if (from_pos == from_span.len && to_pos == to_span.len) {
            // Paths are identical
            str_deinit(alloc, &from_norm);
            str_deinit(alloc, &to_norm);
            return str_init(alloc, ".");
        }
        if (from_pos == from_span.len && (tc == '/' || tc == '\\')) {
            last_sep = from_pos;
        }
    }

    // Count remaining components in from_dir (need that many "..")
    int up_count = 0;
    for (size_t i = last_sep + 1; i < from_span.len; i++) {
        if (from_span.buf[i] == '/' || from_span.buf[i] == '\\') {
            up_count++;
        }
    }
    // If there are characters after last_sep, we need one more ".."
    if (last_sep + 1 < from_span.len) {
        up_count++;
    }

    // Build relative path
    str_build build = str_build_init(alloc, 64);

    // Add ".." components
    for (int i = 0; i < up_count; i++) {
        if (i > 0) str_build_cat(&build, S("/"));
        str_build_cat(&build, S(".."));
    }

    // Add remaining part of to_path
    size_t to_remaining_start = last_sep;
    // Skip the separator
    if (to_remaining_start < to_span.len &&
        (to_span.buf[to_remaining_start] == '/' || to_span.buf[to_remaining_start] == '\\')) {
        to_remaining_start++;
    }

    if (to_remaining_start < to_span.len) {
        if (up_count > 0) {
            str_build_cat(&build, S("/"));
        }
        str remaining =
          str_init_n(alloc, to_span.buf + to_remaining_start, to_span.len - to_remaining_start);
        str_build_cat(&build, remaining);
    }

    // Handle empty result (paths are the same up to the end of from_dir)
    if (build.size == 0) {
        str_build_cat(&build, S("."));
    }

    str_deinit(alloc, &from_norm);
    str_deinit(alloc, &to_norm);

    return str_build_finish(&build);
}

// -- directory scanning --

#define FILE_SCAN_MAX_DEPTH 64

static int has_extension(char const *name, char const *ext) {
    if (!ext) return 1;
    size_t nlen = strlen(name);
    size_t elen = strlen(ext);
    if (nlen < elen) return 0;
    return memcmp(name + nlen - elen, ext, elen) == 0;
}

#ifdef MOS_WINDOWS
static void scan_recursive(allocator *alloc, char const *dir,
                           char const *ext, c_string_carray *out, int depth) {
    if (depth >= FILE_SCAN_MAX_DEPTH) return;

    char pattern[PLATFORM_PATH_MAX];
    int  n = snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    if (n < 0 || (size_t)n >= sizeof(pattern)) return;

    WIN32_FIND_DATAA fd;
    HANDLE           h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;

        char child[PLATFORM_PATH_MAX];
        int  cn = snprintf(child, sizeof(child), "%s\\%s", dir, fd.cFileName);
        if (cn < 0 || (size_t)cn >= sizeof(child)) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            scan_recursive(alloc, child, ext, out, depth + 1);
        } else if (has_extension(fd.cFileName, ext)) {
            size_t len  = strlen(child);
            char  *copy = alloc_malloc(alloc, len + 1);
            memcpy(copy, child, len + 1);
            char const *path = copy;
            array_push(*out, path);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
}
#else
static void scan_recursive(allocator *alloc, char const *dir,
                           char const *ext, c_string_carray *out, int depth) {
    if (depth >= FILE_SCAN_MAX_DEPTH) return;

    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != null) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        char child[PLATFORM_PATH_MAX];
        int  cn = snprintf(child, sizeof(child), "%s/%s", dir, ent->d_name);
        if (cn < 0 || (size_t)cn >= sizeof(child)) continue;

        struct stat st;
        if (lstat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_recursive(alloc, child, ext, out, depth + 1);
        } else if (S_ISREG(st.st_mode) && has_extension(ent->d_name, ext)) {
            size_t len  = strlen(child);
            char  *copy = alloc_malloc(alloc, len + 1);
            memcpy(copy, child, len + 1);
            char const *path = copy;
            array_push(*out, path);
        }
    }

    closedir(d);
}
#endif

void file_scan_dir_recursive(allocator *alloc, char const *dir,
                             char const *ext, c_string_carray *out) {
    scan_recursive(alloc, dir, ext, out, 0);
}
