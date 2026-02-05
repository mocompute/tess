#include "tlib.h"
#include "file.h"
#include "import_resolver.h"
#include "libdeflate.h"
#include "platform.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#endif

#define TLIB_MAGIC         0x544C4942u /* "TLIB" big-endian (network order) */
#define TLIB_VERSION       1u
#define TLIB_HEADER_SIZE   16u
#define TLIB_MAX_FILE_SIZE (64u * 1024u * 1024u)

static inline void write_u32_be(byte *p, u32 v) {
    p[0] = (byte)(v >> 24);
    p[1] = (byte)(v >> 16);
    p[2] = (byte)(v >> 8);
    p[3] = (byte)(v);
}

static inline u32 read_u32_be(byte const *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

int tl_tlib_valid_filename(char const *name, u32 len) {
    if (len == 0) return 0;
    if (name[0] == '/') return 0;
    /* reject Windows absolute paths (e.g. C:\, D:/) */
    if (len >= 2 && name[1] == ':') return 0;
    for (u32 i = 0; i < len; i++) {
        if (name[i] == '\\') return 0;
    }
    /* reject ".." as a path component */
    if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
    for (u32 i = 0; i + 2 < len; i++) {
        if (name[i] == '/' && name[i + 1] == '.' && name[i + 2] == '.') {
            if (i + 3 == len || name[i + 3] == '/') return 0;
        }
    }
    if (len >= 3 && name[0] == '.' && name[1] == '.' && name[2] == '/') return 0;
    return 1;
}

int tl_tlib_write(allocator *alloc, char const *output_path, tl_tlib_entry const *entries, u32 count) {
    /* compute payload size */
    u32 payload_size = 4; /* file count */
    for (u32 i = 0; i < count; i++) {
        payload_size += 4 + entries[i].name_len + 4 + entries[i].data_len;
    }

    /* serialize payload */
    byte *payload = alloc_malloc(alloc, payload_size);
    byte *p       = payload;
    write_u32_be(p, count);
    p += 4;
    for (u32 i = 0; i < count; i++) {
        write_u32_be(p, entries[i].name_len);
        p += 4;
        memcpy(p, entries[i].name, entries[i].name_len);
        p += entries[i].name_len;
        write_u32_be(p, entries[i].data_len);
        p += 4;
        memcpy(p, entries[i].data, entries[i].data_len);
        p += entries[i].data_len;
    }

    /* compress */
    struct libdeflate_compressor *c = libdeflate_alloc_compressor(6);
    if (!c) {
        alloc_free(alloc, payload);
        fprintf(stderr, "tlib: failed to allocate compressor\n");
        return 1;
    }

    size_t bound           = libdeflate_deflate_compress_bound(c, payload_size);
    byte  *compressed      = alloc_malloc(alloc, bound);

    size_t compressed_size = libdeflate_deflate_compress(c, payload, payload_size, compressed, bound);
    libdeflate_free_compressor(c);
    alloc_free(alloc, payload);

    if (compressed_size == 0) {
        alloc_free(alloc, compressed);
        fprintf(stderr, "tlib: compression failed\n");
        return 1;
    }

    /* write file */
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        alloc_free(alloc, compressed);
        perror("tlib: failed to open output file");
        return 1;
    }

    byte header[TLIB_HEADER_SIZE];
    write_u32_be(header + 0, TLIB_MAGIC);
    write_u32_be(header + 4, TLIB_VERSION);
    write_u32_be(header + 8, payload_size);
    write_u32_be(header + 12, (u32)compressed_size);

    int ok = fwrite(header, 1, TLIB_HEADER_SIZE, f) == TLIB_HEADER_SIZE &&
             fwrite(compressed, 1, compressed_size, f) == compressed_size;

    fclose(f);
    alloc_free(alloc, compressed);

    if (!ok) {
        fprintf(stderr, "tlib: failed to write output file\n");
        return 1;
    }
    return 0;
}

int tl_tlib_read(allocator *alloc, char const *input_path, tl_tlib_archive *out) {
    out->entries   = null;
    out->count     = 0;

    char *raw      = null;
    u32   raw_size = 0;
    file_read(alloc, input_path, &raw, &raw_size);
    if (!raw) return 1;

    if (raw_size < TLIB_HEADER_SIZE) {
        fprintf(stderr, "tlib: file too small\n");
        alloc_free(alloc, raw);
        return 1;
    }

    byte const *h               = (byte const *)raw;
    u32         magic           = read_u32_be(h + 0);
    u32         version         = read_u32_be(h + 4);
    u32         uncompressed_sz = read_u32_be(h + 8);
    u32         compressed_sz   = read_u32_be(h + 12);

    if (magic != TLIB_MAGIC) {
        fprintf(stderr, "tlib: invalid magic\n");
        alloc_free(alloc, raw);
        return 1;
    }
    if (version != TLIB_VERSION) {
        fprintf(stderr, "tlib: unsupported version %u\n", version);
        alloc_free(alloc, raw);
        return 1;
    }
    if (compressed_sz != raw_size - TLIB_HEADER_SIZE) {
        fprintf(stderr, "tlib: compressed size mismatch\n");
        alloc_free(alloc, raw);
        return 1;
    }
    if (uncompressed_sz > TLIB_MAX_FILE_SIZE) {
        fprintf(stderr, "tlib: uncompressed size too large\n");
        alloc_free(alloc, raw);
        return 1;
    }

    /* decompress */
    byte                           *payload = alloc_malloc(alloc, uncompressed_sz);

    struct libdeflate_decompressor *d       = libdeflate_alloc_decompressor();
    if (!d) {
        alloc_free(alloc, payload);
        alloc_free(alloc, raw);
        fprintf(stderr, "tlib: failed to allocate decompressor\n");
        return 1;
    }

    size_t                 actual_out = 0;
    enum libdeflate_result r = libdeflate_deflate_decompress(d, raw + TLIB_HEADER_SIZE, compressed_sz,
                                                             payload, uncompressed_sz, &actual_out);
    libdeflate_free_decompressor(d);
    alloc_free(alloc, raw);

    if (r != LIBDEFLATE_SUCCESS || actual_out != uncompressed_sz) {
        fprintf(stderr, "tlib: decompression failed\n");
        alloc_free(alloc, payload);
        return 1;
    }

    /* parse entries */
    byte const *p   = payload;
    byte const *end = payload + uncompressed_sz;

    if (p + 4 > end) goto corrupt;
    u32 count = read_u32_be(p);
    p += 4;

    tl_tlib_entry *entries = alloc_calloc(alloc, count, sizeof(tl_tlib_entry));

    for (u32 i = 0; i < count; i++) {
        if (p + 4 > end) goto corrupt_entries;
        u32 name_len = read_u32_be(p);
        p += 4;
        if (p + name_len > end) goto corrupt_entries;
        char const *name = (char const *)p;
        p += name_len;

        if (!tl_tlib_valid_filename(name, name_len)) {
            fprintf(stderr, "tlib: invalid filename in archive\n");
            alloc_free(alloc, entries);
            alloc_free(alloc, payload);
            return 1;
        }

        if (p + 4 > end) goto corrupt_entries;
        u32 data_len = read_u32_be(p);
        p += 4;
        if (p + data_len > end) goto corrupt_entries;
        byte const *data = p;
        p += data_len;

        entries[i].name     = name;
        entries[i].name_len = name_len;
        entries[i].data     = data;
        entries[i].data_len = data_len;
    }

    out->entries = entries;
    out->count   = count;
    /* caller owns both entries and payload (entries point into payload) */
    return 0;

corrupt_entries:
    alloc_free(alloc, entries);
corrupt:
    fprintf(stderr, "tlib: corrupted payload\n");
    alloc_free(alloc, payload);
    return 1;
}

// -- High-level operations --

int tl_tlib_pack(allocator *alloc, char const *output_path, str_sized files, str base_dir,
                 struct import_resolver *resolver, tl_tlib_pack_opts opts) {
    // Determine base directory from first input file if not provided
    if (str_is_empty(base_dir)) {
        if (files.size == 0) {
            fprintf(stderr, "tlib: no input files\n");
            return 1;
        }
        str first_norm = file_path_normalize(alloc, files.v[0]);
        base_dir       = file_dirname(alloc, first_norm);

        if (str_is_empty(base_dir)) {
            // Use current directory
            char cwd_buf[4096];
            if (!file_current_working_directory((span){.buf = cwd_buf, .len = sizeof(cwd_buf)})) {
                fprintf(stderr, "tlib: failed to determine current working directory\n");
                return 1;
            }
            base_dir = str_init(alloc, cwd_buf);
        }
    }

    if (opts.verbose) {
        fprintf(stderr, "Archive base directory: %s\n", str_cstr(&base_dir));
    }

    // Count non-stdlib files
    u32 user_file_count = 0;
    for (u32 i = 0; i < files.size; i++) {
        if (!import_resolver_is_stdlib_file(resolver, files.v[i])) {
            user_file_count++;
        }
    }

    if (user_file_count == 0) {
        fprintf(stderr, "tlib: no user files to pack (only standard library dependencies)\n");
        return 1;
    }

    // Allocate entries
    tl_tlib_entry *entries   = alloc_malloc(alloc, user_file_count * sizeof(tl_tlib_entry));
    u32            entry_idx = 0;

    // Process each file
    for (u32 i = 0; i < files.size; i++) {
        str file_path = files.v[i];

        // Skip standard library files
        if (import_resolver_is_stdlib_file(resolver, file_path)) {
            if (opts.verbose) {
                fprintf(stderr, "Skipping stdlib: %s\n", str_cstr(&file_path));
            }
            continue;
        }

        // Compute relative path from base directory
        str rel_path = file_path_relative(alloc, base_dir, file_path);

        if (str_is_empty(rel_path)) {
            fprintf(stderr, "tlib: cannot compute relative path for: %s\n", str_cstr(&file_path));
            fprintf(stderr, "      from base directory: %s\n", str_cstr(&base_dir));
            return 1;
        }

        // Validate filename for archive (no "..", no absolute paths)
        if (!tl_tlib_valid_filename(str_cstr(&rel_path), (u32)str_len(rel_path))) {
            fprintf(stderr, "tlib: invalid archive path: %s\n", str_cstr(&rel_path));
            fprintf(stderr, "      Archive paths must not contain '..' or be absolute.\n");
            return 1;
        }

        // Read file contents
        char *data;
        u32   size;
        file_read(alloc, str_cstr(&file_path), &data, &size);
        if (!data) {
            fprintf(stderr, "tlib: failed to read file: %s\n", str_cstr(&file_path));
            return 1;
        }

        if (opts.verbose) {
            fprintf(stderr, "Packing: %s (%u bytes)\n", str_cstr(&rel_path), size);
        }

        // Add entry
        // Note: rel_path may use small string optimization (inline storage),
        // so we must copy it to arena to ensure the pointer stays valid.
        u32   rel_path_len  = (u32)str_len(rel_path);
        char *rel_path_copy = alloc_malloc(alloc, rel_path_len + 1);
        memcpy(rel_path_copy, str_cstr(&rel_path), rel_path_len + 1);
        entries[entry_idx].name     = rel_path_copy;
        entries[entry_idx].name_len = rel_path_len;
        entries[entry_idx].data     = (byte const *)data;
        entries[entry_idx].data_len = size;
        entry_idx++;
    }

    // Write archive
    if (opts.verbose) {
        fprintf(stderr, "Writing archive with %u files to: %s\n", entry_idx, output_path);
    }

    int result = tl_tlib_write(alloc, output_path, entries, entry_idx);

    if (result == 0 && opts.verbose) {
        fprintf(stderr, "Archive created successfully.\n");
    }

    return result;
}

// Helper: create directory and all parent directories
static int mkdir_p(char const *path) {
    char   tmp[4096];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(tmp)) {
        return 1;
    }

    memcpy(tmp, path, len + 1);

    // Create each component
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p         = '\0';

#ifdef MOS_WINDOWS
            int ret = _mkdir(tmp);
#else
            int ret = mkdir(tmp, 0755);
#endif
            if (ret != 0 && errno != EEXIST) {
                return 1;
            }

            *p = saved;
        }
    }

    // Create final directory
#ifdef MOS_WINDOWS
    int ret = _mkdir(tmp);
#else
    int ret = mkdir(tmp, 0755);
#endif
    if (ret != 0 && errno != EEXIST) {
        return 1;
    }

    return 0;
}

int tl_tlib_unpack(allocator *alloc, char const *archive_path, char const *output_dir,
                   tl_tlib_unpack_opts opts) {
    // Read archive
    tl_tlib_archive archive;
    if (tl_tlib_read(alloc, archive_path, &archive) != 0) {
        return 1;
    }

    if (opts.list_only) {
        // List mode: print filenames
        for (u32 i = 0; i < archive.count; i++) {
            printf("%.*s\n", archive.entries[i].name_len, archive.entries[i].name);
        }
        return 0;
    }

    // Extract mode
    if (!output_dir) {
        fprintf(stderr, "tlib: output directory required for extraction\n");
        return 1;
    }

    // Create output directory if needed
    if (mkdir_p(output_dir) != 0) {
        fprintf(stderr, "tlib: failed to create output directory: %s\n", output_dir);
        return 1;
    }

    fprintf(stderr, "tlib: Extracting to: %s\n", output_dir);

    // Extract each file
    for (u32 i = 0; i < archive.count; i++) {
        tl_tlib_entry const *entry = &archive.entries[i];

        // Build output path
        str name     = str_init_n(alloc, entry->name, entry->name_len);
        str out_dir  = str_init(alloc, output_dir);
        str out_path = file_path_join(alloc, out_dir, name);

        // Create parent directory if needed
        str parent = file_dirname(alloc, out_path);
        if (!str_is_empty(parent) && mkdir_p(str_cstr(&parent)) != 0) {
            fprintf(stderr, "error: tlib: failed to create directory: %s\n", str_cstr(&parent));
            return 1;
        }

        // If file exists, exit
        if (file_exists(out_path)) {
            fprintf(stderr, "error: tlib: file exists: %s\n", str_cstr(&out_path));
            return 1;
        }

        // Write file
        FILE *f = fopen(str_cstr(&out_path), "wb");
        if (!f) {
            perror("error: tlib: failed to create file");
            fprintf(stderr, "      path: %s\n", str_cstr(&out_path));
            return 1;
        }

        size_t written = fwrite(entry->data, 1, entry->data_len, f);
        fclose(f);

        if (written != entry->data_len) {
            fprintf(stderr, "error: tlib: failed to write complete file: %s\n", str_cstr(&out_path));
            return 1;
        }

        if (opts.verbose) {
            fprintf(stderr, "Extracted: %s (%u bytes)\n", str_cstr(&name), entry->data_len);
        }
    }

    if (opts.verbose) {
        fprintf(stderr, "Extracted %u files to: %s\n", archive.count, output_dir);
    }

    return 0;
}
