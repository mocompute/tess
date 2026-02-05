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

#define TLIB_MAGIC           0x544C4942u /* "TLIB" big-endian (network order) */
#define TLIB_VERSION         1u
#define TLIB_FIXED_HEADER    8u  /* magic + version */
#define TLIB_MAX_FILE_SIZE   (64u * 1024u * 1024u)
#define TLIB_MAX_META_FIELD  (1u * 1024u * 1024u) /* max metadata field size */

static inline void write_u32_be(byte *p, u32 v) {
    p[0] = (byte)(v >> 24);
    p[1] = (byte)(v >> 16);
    p[2] = (byte)(v >> 8);
    p[3] = (byte)(v);
}

static inline u32 read_u32_be(byte const *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

// Write a length-prefixed string to file. Returns 0 on success.
static int write_string_field(FILE *f, char const *s, u32 len) {
    byte len_buf[4];
    write_u32_be(len_buf, len);
    if (fwrite(len_buf, 1, 4, f) != 4) return 1;
    if (len > 0 && fwrite(s, 1, len, f) != len) return 1;
    return 0;
}

// Read a length-prefixed string from buffer. Updates *pp on success.
// Returns 0 on success, 1 on error.
static int read_string_field(byte const **pp, byte const *end, allocator *alloc, str *out) {
    byte const *p = *pp;
    if (p + 4 > end) return 1;
    u32 len = read_u32_be(p);
    p += 4;
    if (len > TLIB_MAX_META_FIELD) return 1;
    if (p + len > end) return 1;
    if (len > 0) {
        *out = str_init_n(alloc, (char const *)p, len);
    } else {
        *out = str_empty();
    }
    p += len;
    *pp = p;
    return 0;
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

int tl_tlib_write(allocator *alloc, char const *output_path,
                  tl_tlib_metadata const *metadata,
                  tl_tlib_entry const *entries, u32 count) {
    /* compute payload size with overflow check */
    u32 payload_size = 4; /* file count */
    for (u32 i = 0; i < count; i++) {
        u32 entry_size = 8 + entries[i].name_len + entries[i].data_len;
        if (payload_size > UINT32_MAX - entry_size) {
            fprintf(stderr, "tlib: payload too large\n");
            return 1;
        }
        payload_size += entry_size;
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

    /* write fixed header: magic + version */
    byte header[TLIB_FIXED_HEADER];
    write_u32_be(header + 0, TLIB_MAGIC);
    write_u32_be(header + 4, TLIB_VERSION);
    if (fwrite(header, 1, TLIB_FIXED_HEADER, f) != TLIB_FIXED_HEADER) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tlib: failed to write header\n");
        return 1;
    }

    /* write metadata fields (uncompressed) */
    int meta_ok = 1;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->name), (u32)str_len(metadata->name)) == 0;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->author), (u32)str_len(metadata->author)) == 0;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->version), (u32)str_len(metadata->version)) == 0;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->modules), (u32)str_len(metadata->modules)) == 0;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->requires), (u32)str_len(metadata->requires)) == 0;
    meta_ok = meta_ok && write_string_field(f, str_buf(&metadata->requires_optional), (u32)str_len(metadata->requires_optional)) == 0;

    if (!meta_ok) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tlib: failed to write metadata\n");
        return 1;
    }

    /* write payload sizes */
    byte sizes[8];
    write_u32_be(sizes + 0, payload_size);
    write_u32_be(sizes + 4, (u32)compressed_size);
    if (fwrite(sizes, 1, 8, f) != 8) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tlib: failed to write size fields\n");
        return 1;
    }

    /* write compressed payload */
    int ok = fwrite(compressed, 1, compressed_size, f) == compressed_size;

    fclose(f);
    alloc_free(alloc, compressed);

    if (!ok) {
        fprintf(stderr, "tlib: failed to write output file\n");
        return 1;
    }
    return 0;
}

int tl_tlib_read(allocator *alloc, char const *input_path, tl_tlib_archive *out) {
    memset(out, 0, sizeof(*out));

    char *raw      = null;
    u32   raw_size = 0;
    file_read(alloc, input_path, &raw, &raw_size);
    if (!raw) return 1;

    if (raw_size < TLIB_FIXED_HEADER) {
        fprintf(stderr, "tlib: file too small\n");
        alloc_free(alloc, raw);
        return 1;
    }

    byte const *p   = (byte const *)raw;
    byte const *end = p + raw_size;

    /* read fixed header */
    u32 magic   = read_u32_be(p + 0);
    u32 version = read_u32_be(p + 4);
    p += TLIB_FIXED_HEADER;

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

    /* read metadata fields */
    if (read_string_field(&p, end, alloc, &out->metadata.name)) goto corrupt_meta;
    if (read_string_field(&p, end, alloc, &out->metadata.author)) goto corrupt_meta;
    if (read_string_field(&p, end, alloc, &out->metadata.version)) goto corrupt_meta;
    if (read_string_field(&p, end, alloc, &out->metadata.modules)) goto corrupt_meta;
    if (read_string_field(&p, end, alloc, &out->metadata.requires)) goto corrupt_meta;
    if (read_string_field(&p, end, alloc, &out->metadata.requires_optional)) goto corrupt_meta;

    /* read payload sizes */
    if (p + 8 > end) goto corrupt_meta;
    u32 uncompressed_sz = read_u32_be(p);
    u32 compressed_sz   = read_u32_be(p + 4);
    p += 8;

    if (compressed_sz != (u32)(end - p)) {
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
    enum libdeflate_result r          = libdeflate_deflate_decompress(d, p, compressed_sz,
                                                                      payload, uncompressed_sz, &actual_out);
    libdeflate_free_decompressor(d);
    alloc_free(alloc, raw);

    if (r != LIBDEFLATE_SUCCESS || actual_out != uncompressed_sz) {
        fprintf(stderr, "tlib: decompression failed\n");
        alloc_free(alloc, payload);
        return 1;
    }

    /* parse entries */
    byte const *pp  = payload;
    byte const *pend = payload + uncompressed_sz;

    if (pp + 4 > pend) goto corrupt;
    u32 count = read_u32_be(pp);
    pp += 4;

    tl_tlib_entry *entries = alloc_calloc(alloc, count, sizeof(tl_tlib_entry));

    for (u32 i = 0; i < count; i++) {
        if (pp + 4 > pend) goto corrupt_entries;
        u32 name_len = read_u32_be(pp);
        pp += 4;
        if (pp + name_len > pend) goto corrupt_entries;
        char const *name = (char const *)pp;
        pp += name_len;

        if (!tl_tlib_valid_filename(name, name_len)) {
            fprintf(stderr, "tlib: invalid filename in archive\n");
            alloc_free(alloc, entries);
            alloc_free(alloc, payload);
            return 1;
        }

        if (pp + 4 > pend) goto corrupt_entries;
        u32 data_len = read_u32_be(pp);
        pp += 4;
        if (pp + data_len > pend) goto corrupt_entries;
        byte const *data = pp;
        pp += data_len;

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

corrupt_meta:
    fprintf(stderr, "tlib: corrupted metadata\n");
    alloc_free(alloc, raw);
    return 1;
}

// -- High-level operations --

int tl_tlib_pack(allocator *alloc, char const *output_path, str_sized files, str base_dir,
                 struct import_resolver *resolver, tl_tlib_pack_opts opts) {
    // Validate required metadata
    if (!opts.name || strlen(opts.name) == 0) {
        fprintf(stderr, "tlib: --name is required\n");
        return 1;
    }
    if (!opts.version || strlen(opts.version) == 0) {
        fprintf(stderr, "tlib: --pkg-version is required\n");
        return 1;
    }

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

    // Build metadata
    tl_tlib_metadata meta = {
        .name              = str_init(alloc, opts.name),
        .author            = opts.author ? str_init(alloc, opts.author) : str_empty(),
        .version           = str_init(alloc, opts.version),
        .modules           = opts.modules ? str_init(alloc, opts.modules) : str_empty(),
        .requires          = str_empty(),
        .requires_optional = str_empty(),
    };

    // Write archive
    if (opts.verbose) {
        fprintf(stderr, "Writing archive with %u files to: %s\n", entry_idx, output_path);
        fprintf(stderr, "  Name: %s\n", str_cstr(&meta.name));
        fprintf(stderr, "  Version: %s\n", str_cstr(&meta.version));
        if (!str_is_empty(meta.author)) {
            fprintf(stderr, "  Author: %s\n", str_cstr(&meta.author));
        }
        if (!str_is_empty(meta.modules)) {
            fprintf(stderr, "  Modules: %s\n", str_cstr(&meta.modules));
        }
    }

    int result = tl_tlib_write(alloc, output_path, &meta, entries, entry_idx);

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
        // List mode: print metadata and filenames
        tl_tlib_metadata *m = &archive.metadata;
        printf("Name: %s\n", str_cstr(&m->name));
        printf("Version: %s\n", str_cstr(&m->version));
        if (!str_is_empty(m->author)) {
            printf("Author: %s\n", str_cstr(&m->author));
        }
        if (!str_is_empty(m->modules)) {
            printf("Modules: %s\n", str_cstr(&m->modules));
        }
        if (!str_is_empty(m->requires)) {
            printf("Requires: %s\n", str_cstr(&m->requires));
        }
        if (!str_is_empty(m->requires_optional)) {
            printf("Optional: %s\n", str_cstr(&m->requires_optional));
        }
        printf("\nFiles:\n");
        for (u32 i = 0; i < archive.count; i++) {
            printf("  %.*s\n", archive.entries[i].name_len, archive.entries[i].name);
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
