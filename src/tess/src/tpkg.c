#include "tpkg.h"
#include "array.h"
#include "file.h"
#include "hashmap.h"
#include "import_resolver.h"
#include "libdeflate.h"
#include "platform.h"
#include "source_scanner.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define TPKG_MAGIC         0x54504B47u /* "TPKG" big-endian (network order) */
#define TPKG_VERSION       1u
#define TPKG_FIXED_HEADER  8u /* magic + version */
#define TPKG_MAX_FILE_SIZE (64u * 1024u * 1024u)

static void write_u32_be(byte *p, u32 v) {
    p[0] = (byte)(v >> 24);
    p[1] = (byte)(v >> 16);
    p[2] = (byte)(v >> 8);
    p[3] = (byte)(v);
}

static u32 read_u32_be(byte const *p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static void write_u16_be(byte *p, u16 v) {
    p[0] = (byte)(v >> 8);
    p[1] = (byte)(v);
}

static u16 read_u16_be(byte const *p) {
    return ((u16)p[0] << 8) | (u16)p[1];
}

// Write bytes to file and update CRC32. Returns 0 on success.
static int fwrite_crc(FILE *f, void const *data, size_t len, u32 *crc) {
    if (fwrite(data, 1, len, f) != len) return 1;
    *crc = libdeflate_crc32(*crc, data, len);
    return 0;
}

// Write a u16-length-prefixed string to file, updating CRC32. Returns 0 on success.
static int write_str16(FILE *f, char const *s, u16 len, u32 *crc) {
    byte len_buf[2];
    write_u16_be(len_buf, len);
    if (fwrite_crc(f, len_buf, 2, crc)) return 1;
    if (len > 0 && fwrite_crc(f, s, len, crc)) return 1;
    return 0;
}

// Write a u16-counted array of u16-prefixed strings, updating CRC32. Returns 0 on success.
static int write_str16_array(FILE *f, str const *items, u16 count, u32 *crc) {
    byte count_buf[2];
    write_u16_be(count_buf, count);
    if (fwrite_crc(f, count_buf, 2, crc)) return 1;
    for (u16 i = 0; i < count; i++) {
        size_t slen = str_len(items[i]);
        if (slen > UINT16_MAX) return 1;
        if (write_str16(f, str_buf(&items[i]), (u16)slen, crc)) return 1;
    }
    return 0;
}

// Read a u16-length-prefixed string from buffer. Updates *pp on success.
static int read_str16(byte const **pp, byte const *end, allocator *alloc, str *out) {
    byte const *p = *pp;
    if (p + 2 > end) return 1;
    u16 len = read_u16_be(p);
    p += 2;
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

// Read a u16-counted array of u16-prefixed strings. Returns 0 on success.
static int read_str16_array(byte const **pp, byte const *end, allocator *alloc, str **out_items,
                            u16 *out_count) {
    byte const *p = *pp;
    if (p + 2 > end) return 1;
    u16 count = read_u16_be(p);
    p += 2;
    *out_count = count;
    if (count == 0) {
        *out_items = null;
        *pp        = p;
        return 0;
    }
    str *items = alloc_calloc(alloc, count, sizeof(str));
    for (u16 i = 0; i < count; i++) {
        if (read_str16(&p, end, alloc, &items[i])) {
            alloc_free(alloc, items);
            return 1;
        }
    }
    *out_items = items;
    *pp        = p;
    return 0;
}

int tl_tpkg_valid_filename(char const *name, u32 len) {
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

int tl_tpkg_write(allocator *alloc, char const *output_path, tl_tpkg_metadata const *metadata,
                  tl_tpkg_entry const *entries, u32 count) {
    /* compute payload size with overflow check */
    u32 payload_size = 4; /* file count */
    for (u32 i = 0; i < count; i++) {
        u32 entry_size = 8 + entries[i].name_len + entries[i].data_len;
        if (payload_size > UINT32_MAX - entry_size) {
            fprintf(stderr, "tpkg: payload too large\n");
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
        fprintf(stderr, "tpkg: failed to allocate compressor\n");
        return 1;
    }

    size_t bound           = libdeflate_deflate_compress_bound(c, payload_size);
    byte  *compressed      = alloc_malloc(alloc, bound);

    size_t compressed_size = libdeflate_deflate_compress(c, payload, payload_size, compressed, bound);
    libdeflate_free_compressor(c);
    alloc_free(alloc, payload);

    if (compressed_size == 0) {
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: compression failed\n");
        return 1;
    }

    /* validate metadata string lengths fit in u16 */
    if (str_len(metadata->name) > UINT16_MAX || str_len(metadata->author) > UINT16_MAX ||
        str_len(metadata->version) > UINT16_MAX) {
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: metadata field too long for u16\n");
        return 1;
    }

    /* write file */
    FILE *f = fopen(output_path, "wb");
    if (!f) {
        alloc_free(alloc, compressed);
        perror("tpkg: failed to open output file");
        return 1;
    }

    u32 crc = 0;

    /* write fixed header: magic + version */
    byte header[TPKG_FIXED_HEADER];
    write_u32_be(header + 0, TPKG_MAGIC);
    write_u32_be(header + 4, TPKG_VERSION);
    if (fwrite_crc(f, header, TPKG_FIXED_HEADER, &crc)) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: failed to write header\n");
        return 1;
    }

    /* write metadata fields (uncompressed, u16-prefixed) */
    int meta_ok = 1;
    meta_ok = meta_ok && write_str16(f, str_buf(&metadata->name), (u16)str_len(metadata->name), &crc) == 0;
    meta_ok =
      meta_ok && write_str16(f, str_buf(&metadata->author), (u16)str_len(metadata->author), &crc) == 0;
    meta_ok =
      meta_ok && write_str16(f, str_buf(&metadata->version), (u16)str_len(metadata->version), &crc) == 0;
    meta_ok = meta_ok && write_str16_array(f, metadata->modules, metadata->module_count, &crc) == 0;
    meta_ok = meta_ok && write_str16_array(f, metadata->depends, metadata->depends_count, &crc) == 0;

    if (!meta_ok) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: failed to write metadata\n");
        return 1;
    }

    /* write payload sizes */
    byte sizes[8];
    write_u32_be(sizes + 0, payload_size);
    write_u32_be(sizes + 4, (u32)compressed_size);
    if (fwrite_crc(f, sizes, 8, &crc)) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: failed to write size fields\n");
        return 1;
    }

    /* write compressed payload */
    if (fwrite(compressed, 1, compressed_size, f) != compressed_size) {
        fclose(f);
        alloc_free(alloc, compressed);
        fprintf(stderr, "tpkg: failed to write compressed payload\n");
        return 1;
    }
    crc = libdeflate_crc32(crc, compressed, compressed_size);

    alloc_free(alloc, compressed);

    /* write CRC32 checksum */
    byte crc_buf[4];
    write_u32_be(crc_buf, crc);
    if (fwrite(crc_buf, 1, 4, f) != 4) {
        fclose(f);
        fprintf(stderr, "tpkg: failed to write CRC32\n");
        return 1;
    }

    fclose(f);
    return 0;
}

// Parse a tpkg archive from raw bytes (must include header and CRC32 trailer).
// Does not free raw_buf. Returns 0 on success.
static int tl_tpkg_parse(allocator *alloc, byte const *raw_buf, u32 raw_size, tl_tpkg_archive *out) {
    memset(out, 0, sizeof(*out));

    /* need at least header(8) + CRC32(4) */
    if (raw_size < TPKG_FIXED_HEADER + 4) {
        fprintf(stderr, "tpkg: file too small\n");
        return 1;
    }

    /* verify CRC32: checksum covers all bytes except the trailing 4 */
    u32 stored_crc   = read_u32_be(raw_buf + raw_size - 4);
    u32 computed_crc = libdeflate_crc32(0, raw_buf, raw_size - 4);
    if (stored_crc != computed_crc) {
        fprintf(stderr, "tpkg: CRC32 checksum mismatch (archive corrupted)\n");
        return 1;
    }

    byte const *p   = raw_buf;
    byte const *end = p + raw_size - 4; /* exclude CRC32 from field parsing */

    /* read fixed header */
    u32 magic   = read_u32_be(p + 0);
    u32 version = read_u32_be(p + 4);
    p += TPKG_FIXED_HEADER;

    if (magic != TPKG_MAGIC) {
        fprintf(stderr, "tpkg: invalid magic\n");
        return 1;
    }
    if (version != TPKG_VERSION) {
        fprintf(stderr, "tpkg: unsupported version %u\n", version);
        return 1;
    }

    /* read metadata fields (u16-prefixed) */
    if (read_str16(&p, end, alloc, &out->metadata.name)) goto corrupt_meta;
    if (read_str16(&p, end, alloc, &out->metadata.author)) goto corrupt_meta;
    if (read_str16(&p, end, alloc, &out->metadata.version)) goto corrupt_meta;
    if (read_str16_array(&p, end, alloc, &out->metadata.modules, &out->metadata.module_count))
        goto corrupt_meta;
    if (read_str16_array(&p, end, alloc, &out->metadata.depends, &out->metadata.depends_count))
        goto corrupt_meta;

    /* read payload sizes */
    if (p + 8 > end) goto corrupt_meta;
    u32 uncompressed_sz = read_u32_be(p);
    u32 compressed_sz   = read_u32_be(p + 4);
    p += 8;

    if (compressed_sz != (u32)(end - p)) {
        fprintf(stderr, "tpkg: compressed size mismatch\n");
        return 1;
    }
    if (uncompressed_sz > TPKG_MAX_FILE_SIZE) {
        fprintf(stderr, "tpkg: uncompressed size too large\n");
        return 1;
    }

    /* decompress */
    byte                           *payload = alloc_malloc(alloc, uncompressed_sz);

    struct libdeflate_decompressor *d       = libdeflate_alloc_decompressor();
    if (!d) {
        alloc_free(alloc, payload);
        fprintf(stderr, "tpkg: failed to allocate decompressor\n");
        return 1;
    }

    size_t                 actual_out = 0;
    enum libdeflate_result r =
      libdeflate_deflate_decompress(d, p, compressed_sz, payload, uncompressed_sz, &actual_out);
    libdeflate_free_decompressor(d);

    if (r != LIBDEFLATE_SUCCESS || actual_out != uncompressed_sz) {
        fprintf(stderr, "tpkg: decompression failed\n");
        alloc_free(alloc, payload);
        return 1;
    }

    /* parse entries */
    byte const *pp   = payload;
    byte const *pend = payload + uncompressed_sz;

    if (pp + 4 > pend) goto corrupt;
    u32 entry_count = read_u32_be(pp);
    pp += 4;

    tl_tpkg_entry *entries = entry_count ? alloc_calloc(alloc, entry_count, sizeof(tl_tpkg_entry)) : null;

    for (u32 i = 0; i < entry_count; i++) {
        if (pp + 4 > pend) goto corrupt_entries;
        u32 name_len = read_u32_be(pp);
        pp += 4;
        if (pp + name_len > pend) goto corrupt_entries;
        char const *name = (char const *)pp;
        pp += name_len;

        if (!tl_tpkg_valid_filename(name, name_len)) {
            fprintf(stderr, "tpkg: invalid filename in archive\n");
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

        entries[i].name_len = name_len;
        entries[i].name     = alloc_malloc(alloc, name_len);
        memcpy((char *)entries[i].name, name, name_len);

        entries[i].data_len = data_len;
        if (data_len > 0) {
            entries[i].data = alloc_malloc(alloc, data_len);
            memcpy((char *)entries[i].data, data, data_len);
        } else {
            entries[i].data = null;
        }
    }

    out->entries       = entries;
    out->entries_count = entry_count;
    alloc_free(alloc, payload);
    return 0;

corrupt_entries:
    alloc_free(alloc, entries);
corrupt:
    fprintf(stderr, "tpkg: corrupted payload\n");
    alloc_free(alloc, payload);
    return 1;

corrupt_meta:
    fprintf(stderr, "tpkg: corrupted metadata\n");
    return 1;
}

int tl_tpkg_read(allocator *alloc, char const *input_path, tl_tpkg_archive *out) {
    char *raw      = null;
    u32   raw_size = 0;
    file_read(alloc, input_path, &raw, &raw_size);
    if (!raw) return 1;

    int result = tl_tpkg_parse(alloc, (byte const *)raw, raw_size, out);
    alloc_free(alloc, raw);
    return result;
}

int tl_tpkg_read_from_memory(allocator *alloc, void const *data, u32 size, tl_tpkg_archive *out) {
    return tl_tpkg_parse(alloc, (byte const *)data, size, out);
}

// -- High-level operations --

// Verify all quoted imports in archive entries resolve to other entries.
// Uses tl_source_scanner_collect_imports() for correct string/comment handling.
// Returns 0 if self-contained, 1 if an import escapes the archive.
static int check_self_containment(allocator *alloc, tl_tpkg_entry const *entries, u32 count) {
    int error = 0;

    // Build hashset of entry names
    hashmap *entry_names = hset_create(alloc, count * 2);
    for (u32 i = 0; i < count; i++) {
        str name = str_init_n(alloc, entries[i].name, entries[i].name_len);
        str_hset_insert(&entry_names, name);
        str_deinit(alloc, &name);
    }

    // Scan each entry for #import directives
    for (u32 i = 0; i < count; i++) {
        str_array imports = {.alloc = alloc};
        tl_source_scanner_collect_imports(
          alloc, (char_csized){(char const *)entries[i].data, entries[i].data_len}, &imports);

        str entry_name = str_init_n(alloc, entries[i].name, entries[i].name_len);
        str entry_dir  = file_dirname(alloc, entry_name);

        for (u32 j = 0; j < imports.size; j++) {
            str  imp   = imports.v[j];
            span s     = str_span(&imp);
            char first = (char)(s.len > 0 ? s.buf[0] : 0);

            // Skip non-quoted imports (angle-bracket = stdlib, not in archive)
            if (first != '"') continue;

            // Strip surrounding quotes to get raw path
            if (s.len < 3 || s.buf[s.len - 1] != '"') continue;
            str import_path = str_init_n(alloc, s.buf + 1, (u32)s.len - 2);

            // Resolve relative to importing file's directory
            str joined     = file_path_join(alloc, entry_dir, import_path);
            str normalized = file_path_normalize(alloc, joined);

            if (str_is_empty(normalized) || !str_hset_contains(entry_names, normalized)) {
                fprintf(stderr,
                        "error: self-containment check failed: '%s' (imported from '%s') "
                        "not found in archive\n",
                        str_cstr(&import_path), entries[i].name);
                error = 1;
                // fallthrough to break
            }

            if (error) break;
        }

        forall(i, imports) str_deinit(alloc, &imports.v[i]);
        array_free(imports);

        str_deinit(alloc, &entry_dir);
        str_deinit(alloc, &entry_name);
        if (error) break;
    }

    hset_destroy(&entry_names);
    return error;
}

int tl_tpkg_pack(allocator *alloc, char const *output_path, str_sized files, str base_dir,
                 struct import_resolver *resolver, tl_tpkg_pack_opts opts) {
    // Validate required metadata
    if (!opts.name || strlen(opts.name) == 0) {
        fprintf(stderr, "tpkg: name is required\n");
        return 1;
    }
    if (!opts.version || strlen(opts.version) == 0) {
        fprintf(stderr, "tpkg: package version is required\n");
        return 1;
    }

    // Determine base directory from first input file if not provided
    if (str_is_empty(base_dir)) {
        if (files.size == 0) {
            fprintf(stderr, "tpkg: no input files\n");
            return 1;
        }
        str first_norm = file_path_normalize(alloc, files.v[0]);
        base_dir       = file_dirname(alloc, first_norm);

        if (str_is_empty(base_dir)) {
            // Use current directory
            char cwd_buf[4096];
            if (!file_current_working_directory((span){.buf = cwd_buf, .len = sizeof(cwd_buf)})) {
                fprintf(stderr, "tpkg: failed to determine current working directory\n");
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
        fprintf(stderr, "tpkg: no user files to pack (only standard library dependencies)\n");
        return 1;
    }

    // Allocate entries
    tl_tpkg_entry *entries   = alloc_malloc(alloc, user_file_count * sizeof(tl_tpkg_entry));
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
            fprintf(stderr, "tpkg: cannot compute relative path for: %s\n", str_cstr(&file_path));
            fprintf(stderr, "      from base directory: %s\n", str_cstr(&base_dir));
            return 1;
        }

        // Validate filename for archive (no "..", no absolute paths)
        if (!tl_tpkg_valid_filename(str_cstr(&rel_path), (u32)str_len(rel_path))) {
            fprintf(stderr, "tpkg: invalid archive path: %s\n", str_cstr(&rel_path));
            fprintf(stderr, "      Archive paths must not contain '..' or be absolute.\n");
            return 1;
        }

        // Read file contents
        char *data;
        u32   size;
        file_read(alloc, str_cstr(&file_path), &data, &size);
        if (!data) {
            fprintf(stderr, "tpkg: failed to read file: %s\n", str_cstr(&file_path));
            return 1;
        }

        if (opts.verbose) {
            fprintf(stderr, "Packing: %s (%u bytes)\n", str_cstr(&rel_path), size);
        }

        // Add entry
        // Note: rel_path may use small string optimization (inline storage),
        // so we must copy it to arena to ensure the pointer stays valid.
        entries[entry_idx].name     = str_cstr_copy(alloc, rel_path);
        entries[entry_idx].name_len = (u32)str_len(rel_path);
        entries[entry_idx].data     = (byte const *)data;
        entries[entry_idx].data_len = size;
        entry_idx++;
    }

    // Self-containment check: verify all quoted imports resolve within the archive
    if (check_self_containment(alloc, entries, entry_idx)) {
        return 1;
    }

    // Append package.tl as an entry (if provided)
    if (opts.package_tl_path) {
        char *pkg_data;
        u32   pkg_size;
        file_read(alloc, opts.package_tl_path, &pkg_data, &pkg_size);
        if (!pkg_data) {
            fprintf(stderr, "tpkg: failed to read package.tl: %s\n", opts.package_tl_path);
            return 1;
        }

        entries                 = alloc_realloc(alloc, entries, (entry_idx + 1) * sizeof(tl_tpkg_entry));
        entries[entry_idx].name = "package.tl";
        entries[entry_idx].name_len = 10;
        entries[entry_idx].data     = (byte const *)pkg_data;
        entries[entry_idx].data_len = pkg_size;
        entry_idx++;

        if (opts.verbose) {
            fprintf(stderr, "Packing: package.tl (%u bytes)\n", pkg_size);
        }
    }

    // Build metadata
    tl_tpkg_metadata meta = {
      .name          = str_init(alloc, opts.name),
      .author        = opts.author ? str_init(alloc, opts.author) : str_empty(),
      .version       = str_init(alloc, opts.version),
      .modules       = opts.modules,
      .module_count  = opts.module_count,
      .depends       = opts.depends,
      .depends_count = opts.depends_count,
    };

    // Write archive
    if (opts.verbose) {
        fprintf(stderr, "Writing archive with %u files to: %s\n", entry_idx, output_path);
        fprintf(stderr, "  Name: %s\n", str_cstr(&meta.name));
        fprintf(stderr, "  Version: %s\n", str_cstr(&meta.version));
        if (!str_is_empty(meta.author)) {
            fprintf(stderr, "  Author: %s\n", str_cstr(&meta.author));
        }
        if (meta.module_count > 0) {
            fprintf(stderr, "  Modules:");
            for (u16 i = 0; i < meta.module_count; i++) {
                fprintf(stderr, " %s", str_cstr(&meta.modules[i]));
            }
            fprintf(stderr, "\n");
        }
    }

    int result = tl_tpkg_write(alloc, output_path, &meta, entries, entry_idx);

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

    // Skip drive letter on Windows (e.g. "C:\") — _mkdir("C:") can fail with
    // EACCES instead of EEXIST on some environments (e.g. GitHub Actions runners).
    char *start = tmp + 1;
#ifdef MOS_WINDOWS
    if (len >= 3 && tmp[1] == ':' && (tmp[2] == '/' || tmp[2] == '\\')) {
        start = tmp + 3;
    }
#endif

    // Create each component
    for (char *p = start; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p         = '\0';

            int ret    = platform_mkdir(tmp);
            if (ret != 0 && errno != EEXIST) {
                return 1;
            }

            *p = saved;
        }
    }

    // Create final directory
    int ret = platform_mkdir(tmp);
    if (ret != 0 && errno != EEXIST) {
        return 1;
    }

    return 0;
}

int tl_tpkg_extract(allocator *alloc, tl_tpkg_archive const *archive, char const *output_dir,
                    str_array *out_files) {
    if (mkdir_p(output_dir) != 0) {
        fprintf(stderr, "tpkg: failed to create output directory: %s\n", output_dir);
        return 1;
    }

    str out_dir = str_init(alloc, output_dir);

    for (u32 i = 0; i < archive->entries_count; i++) {
        tl_tpkg_entry const *entry    = &archive->entries[i];

        str                  name     = str_init_n(alloc, entry->name, entry->name_len);
        str                  out_path = file_path_join(alloc, out_dir, name);

        str                  parent   = file_dirname(alloc, out_path);
        if (!str_is_empty(parent) && mkdir_p(str_cstr(&parent)) != 0) {
            fprintf(stderr, "tpkg: failed to create directory: %s\n", str_cstr(&parent));
            return 1;
        }

        FILE *f = fopen(str_cstr(&out_path), "wb");
        if (!f) {
            fprintf(stderr, "tpkg: failed to create file: %s\n", str_cstr(&out_path));
            return 1;
        }

        size_t written = fwrite(entry->data, 1, entry->data_len, f);
        fclose(f);

        if (written != entry->data_len) {
            fprintf(stderr, "tpkg: failed to write file: %s\n", str_cstr(&out_path));
            return 1;
        }

        // Skip package.tl from compilation list (it's not a source file)
        if (entry->name_len == 10 && memcmp(entry->name, "package.tl", 10) == 0) {
            continue;
        }

        if (out_files) {
            str normed = file_path_normalize(alloc, out_path);
            array_push(*out_files, normed);
        }
        str_deinit(alloc, &out_path);
    }

    return 0;
}

int tl_tpkg_unpack(allocator *alloc, char const *archive_path, char const *output_dir,
                   tl_tpkg_unpack_opts opts) {
    // Read archive
    tl_tpkg_archive archive;
    if (tl_tpkg_read(alloc, archive_path, &archive) != 0) {
        return 1;
    }

    if (opts.list_only) {
        // List mode: print metadata and filenames
        tl_tpkg_metadata *m = &archive.metadata;
        printf("Name: %s\n", str_cstr(&m->name));
        printf("Version: %s\n", str_cstr(&m->version));
        if (!str_is_empty(m->author)) {
            printf("Author: %s\n", str_cstr(&m->author));
        }
        if (m->module_count > 0) {
            printf("Modules:\n");
            for (u16 i = 0; i < m->module_count; i++) {
                printf("  %s\n", str_cstr(&m->modules[i]));
            }
        }
        if (m->depends_count > 0) {
            printf("Requires:\n");
            for (u16 i = 0; i < m->depends_count; i++) {
                printf("  %s\n", str_cstr(&m->depends[i]));
            }
        }
        printf("\nFiles:\n");
        for (u32 i = 0; i < archive.entries_count; i++) {
            printf("  %.*s\n", archive.entries[i].name_len, archive.entries[i].name);
        }
        return 0;
    }

    // Extract mode
    if (!output_dir) {
        fprintf(stderr, "tpkg: output directory required for extraction\n");
        return 1;
    }

    // Create output directory if needed
    if (mkdir_p(output_dir) != 0) {
        fprintf(stderr, "tpkg: failed to create output directory: %s\n", output_dir);
        return 1;
    }

    fprintf(stderr, "tpkg: Extracting to: %s\n", output_dir);

    // Extract each file
    for (u32 i = 0; i < archive.entries_count; i++) {
        tl_tpkg_entry const *entry = &archive.entries[i];

        // Build output path
        str name     = str_init_n(alloc, entry->name, entry->name_len);
        str out_dir  = str_init(alloc, output_dir);
        str out_path = file_path_join(alloc, out_dir, name);

        // Create parent directory if needed
        str parent = file_dirname(alloc, out_path);
        if (!str_is_empty(parent) && mkdir_p(str_cstr(&parent)) != 0) {
            fprintf(stderr, "error: tpkg: failed to create directory: %s\n", str_cstr(&parent));
            return 1;
        }

        // If file exists, exit
        if (file_exists(out_path)) {
            fprintf(stderr, "error: tpkg: file exists: %s\n", str_cstr(&out_path));
            return 1;
        }

        // Write file
        FILE *f = fopen(str_cstr(&out_path), "wb");
        if (!f) {
            perror("error: tpkg: failed to create file");
            fprintf(stderr, "      path: %s\n", str_cstr(&out_path));
            return 1;
        }

        size_t written = fwrite(entry->data, 1, entry->data_len, f);
        fclose(f);

        if (written != entry->data_len) {
            fprintf(stderr, "error: tpkg: failed to write complete file: %s\n", str_cstr(&out_path));
            return 1;
        }

        if (opts.verbose) {
            fprintf(stderr, "Extracted: %s (%u bytes)\n", str_cstr(&name), entry->data_len);
        }
    }

    if (opts.verbose) {
        fprintf(stderr, "Extracted %u files to: %s\n", archive.entries_count, output_dir);
    }

    return 0;
}

int tl_tpkg_parse_dep_string(allocator *alloc, str dep_str, str *out_name, str *out_version) {
    char const *s  = str_cstr(&dep_str);
    char const *eq = strchr(s, '=');
    if (!eq || eq == s || !eq[1]) return 1;
    *out_name    = str_init_n(alloc, s, (size_t)(eq - s));
    *out_version = str_init_n(alloc, eq + 1, str_len(dep_str) - (size_t)(eq - s) - 1);
    return 0;
}

str tl_tpkg_filename(allocator *alloc, str name, str version) {
    return str_cat_4(alloc, name, S("-"), version, S(".tpkg"));
}
