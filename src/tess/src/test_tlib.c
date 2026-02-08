#include "array.h"
#include "file.h"
#include "import_resolver.h"
#include "manifest.h"
#include "platform.h"
#include "tlib.h"

#include <stdio.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <io.h>
#define ftruncate(fd, size) _chsize(fd, size)
#define fileno              _fileno
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define T(name)                                                                                            \
    this_error = name();                                                                                   \
    if (this_error) {                                                                                      \
        fprintf(stderr, "FAILED: %s\n", #name);                                                            \
        error += this_error;                                                                               \
    }

static char temp_dir[512];

static void init_temp_dir(void) {
    platform_temp_dir(temp_dir, sizeof(temp_dir));
}

static void make_temp_path(char *buf, size_t bufsize, char const *filename) {
    snprintf(buf, bufsize, "%s%s", temp_dir, filename);
}

static str              test_modules[2];

static tl_tlib_metadata make_test_metadata(allocator *alloc) {
    test_modules[0] = str_init(alloc, "Foo");
    test_modules[1] = str_init(alloc, "Bar");
    return (tl_tlib_metadata){
      .name                   = str_init(alloc, "TestLib"),
      .author                 = str_init(alloc, "Tester"),
      .version                = str_init(alloc, "1.0.0"),
      .modules                = test_modules,
      .module_count           = 2,
      .depends                = null,
      .depends_count          = 0,
      .depends_optional       = null,
      .depends_optional_count = 0,
    };
}

static int test_roundtrip(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_roundtrip.tlib");

    tl_tlib_metadata meta       = make_test_metadata(alloc);

    tl_tlib_entry    entries[3] = {
      {"hello.tl", 8, (byte const *)"hello!\n", 7},
      {"sub/world.tl", 12, (byte const *)"world!\n", 7},
      {"empty.tl", 8, (byte const *)"", 0},
    };

    if (tl_tlib_write(alloc, path, &meta, entries, 3)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    if (arc.entries_count != 3) {
        fprintf(stderr, "  expected 3 entries, got %u\n", arc.entries_count);
        return 1;
    }

    // Verify metadata roundtrip
    if (!str_eq(arc.metadata.name, meta.name)) {
        fprintf(stderr, "  metadata.name mismatch\n");
        return 1;
    }
    if (!str_eq(arc.metadata.version, meta.version)) {
        fprintf(stderr, "  metadata.version mismatch\n");
        return 1;
    }

    for (u32 i = 0; i < 3; i++) {
        if (arc.entries[i].name_len != entries[i].name_len ||
            memcmp(arc.entries[i].name, entries[i].name, entries[i].name_len) != 0) {
            fprintf(stderr, "  name mismatch at entry %u\n", i);
            return 1;
        }
        if (arc.entries[i].data_len != entries[i].data_len ||
            memcmp(arc.entries[i].data, entries[i].data, entries[i].data_len) != 0) {
            fprintf(stderr, "  data mismatch at entry %u\n", i);
            return 1;
        }
    }

    return 0;
}

static int test_empty_archive(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_empty.tlib");

    tl_tlib_metadata meta = make_test_metadata(alloc);

    if (tl_tlib_write(alloc, path, &meta, null, 0)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    if (arc.entries_count != 0) {
        fprintf(stderr, "  expected 0 entries, got %u\n", arc.entries_count);
        return 1;
    }

    return 0;
}

static int test_filename_validation(void) {
    int error = 0;

    /* valid names */
    error += tl_tlib_valid_filename("foo.tl", 6) != 1;
    error += tl_tlib_valid_filename("sub/bar.tl", 10) != 1;
    error += tl_tlib_valid_filename("a/b/c.tl", 8) != 1;
    error += tl_tlib_valid_filename("..x", 3) != 1; // weird but legal regular hidden filename

    /* invalid names */
    error += tl_tlib_valid_filename("", 0) != 0;
    error += tl_tlib_valid_filename("/etc/passwd", 11) != 0;
    error += tl_tlib_valid_filename("../foo", 6) != 0;
    error += tl_tlib_valid_filename("foo/../bar", 10) != 0;
    error += tl_tlib_valid_filename("..", 2) != 0;
    error += tl_tlib_valid_filename("foo\\bar", 7) != 0;
    error += tl_tlib_valid_filename("C:\\foo", 6) != 0;
    error += tl_tlib_valid_filename("D:/bar.tl", 9) != 0;

    if (error) fprintf(stderr, "  %d filename validation check(s) failed\n", error);

    return error;
}

static int test_byte_order(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_byteorder.tlib");

    tl_tlib_metadata meta  = make_test_metadata(alloc);
    tl_tlib_entry    entry = {"a.tl", 4, (byte const *)"x", 1};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    // Read raw bytes and verify header is big-endian
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "  open failed\n");
        return 1;
    }

    byte header[8];
    if (fread(header, 1, 8, f) != 8) {
        fclose(f);
        fprintf(stderr, "  read failed\n");
        return 1;
    }
    fclose(f);

    // Verify magic is "TLIB" in reading order (big-endian)
    if (header[0] != 'T' || header[1] != 'L' || header[2] != 'I' || header[3] != 'B') {
        fprintf(stderr, "  magic mismatch: expected TLIB, got %c%c%c%c\n", header[0], header[1], header[2],
                header[3]);
        return 1;
    }

    // Verify version is 1 in big-endian (0x00000001)
    if (header[4] != 0 || header[5] != 0 || header[6] != 0 || header[7] != 1) {
        fprintf(stderr, "  version mismatch\n");
        return 1;
    }

    return 0;
}

static int test_large_payload(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_large.tlib");

    tl_tlib_metadata meta = make_test_metadata(alloc);

    u32              size = 1024 * 1024;
    byte            *data = alloc_malloc(alloc, size);
    for (u32 i = 0; i < size; i++) data[i] = (byte)(i * 7 + 13);

    tl_tlib_entry entry = {"big.tl", 6, data, size};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        alloc_free(alloc, data);
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        alloc_free(alloc, data);
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    int result = 0;
    if (arc.entries_count != 1 || arc.entries[0].data_len != size ||
        memcmp(arc.entries[0].data, data, size) != 0) {
        fprintf(stderr, "  large payload data mismatch\n");
        result = 1;
    }

    alloc_free(alloc, data);
    return result;
}

static int test_metadata_roundtrip(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_metadata.tlib");

    str modules[3];
    modules[0] = str_init(alloc, "Core");
    modules[1] = str_init(alloc, "Utils");
    modules[2] = str_init(alloc, "Helper");

    str depends[1];
    depends[0] = str_init(alloc, "Lib=1.0.0");

    str depends_opt[1];
    depends_opt[0]        = str_init(alloc, "Debug=0.5.0");

    tl_tlib_metadata meta = {
      .name                   = str_init(alloc, "MetaTest"),
      .author                 = str_init(alloc, "Alice"),
      .version                = str_init(alloc, "2.3.4"),
      .modules                = modules,
      .module_count           = 3,
      .depends                = depends,
      .depends_count          = 1,
      .depends_optional       = depends_opt,
      .depends_optional_count = 1,
    };

    tl_tlib_entry entry = {"test.tl", 7, (byte const *)"content", 7};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    int error = 0;
    error += !str_eq(arc.metadata.name, meta.name);
    error += !str_eq(arc.metadata.author, meta.author);
    error += !str_eq(arc.metadata.version, meta.version);
    error += (arc.metadata.module_count != 3);
    for (u16 i = 0; i < 3 && i < arc.metadata.module_count; i++) {
        error += !str_eq(arc.metadata.modules[i], modules[i]);
    }
    error += (arc.metadata.depends_count != 1);
    if (arc.metadata.depends_count >= 1) {
        error += !str_eq(arc.metadata.depends[0], depends[0]);
    }
    error += (arc.metadata.depends_optional_count != 1);
    if (arc.metadata.depends_optional_count >= 1) {
        error += !str_eq(arc.metadata.depends_optional[0], depends_opt[0]);
    }

    if (error) {
        fprintf(stderr, "  metadata mismatch (%d fields)\n", error);
    }

    return error;
}

static int test_metadata_empty_fields(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_metadata_empty.tlib");

    tl_tlib_metadata meta = {
      .name                   = str_init(alloc, "MinimalLib"),
      .author                 = str_empty(),
      .version                = str_init(alloc, "0.1"),
      .modules                = null,
      .module_count           = 0,
      .depends                = null,
      .depends_count          = 0,
      .depends_optional       = null,
      .depends_optional_count = 0,
    };

    tl_tlib_entry entry = {"a.tl", 4, (byte const *)"x", 1};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    int error = 0;
    error += !str_eq(arc.metadata.name, meta.name);
    error += !str_is_empty(arc.metadata.author);
    error += !str_eq(arc.metadata.version, meta.version);
    error += (arc.metadata.module_count != 0);
    error += (arc.metadata.modules != null);
    error += (arc.metadata.depends_count != 0);
    error += (arc.metadata.depends_optional_count != 0);

    if (error) {
        fprintf(stderr, "  empty field handling failed (%d errors)\n", error);
    }

    return error;
}

static int test_metadata_unicode(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_metadata_unicode.tlib");

    str unicode_modules[2];
    unicode_modules[0]    = str_init(alloc, "Módulo");
    unicode_modules[1]    = str_init(alloc, "Функция");

    tl_tlib_metadata meta = {
      .name                   = str_init(alloc, "Bibliothèque"),
      .author                 = str_init(alloc, "日本語 Author™"),
      .version                = str_init(alloc, "1.0.0-β"),
      .modules                = unicode_modules,
      .module_count           = 2,
      .depends                = null,
      .depends_count          = 0,
      .depends_optional       = null,
      .depends_optional_count = 0,
    };

    tl_tlib_entry entry = {"test.tl", 7, (byte const *)"content", 7};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    int error = 0;
    error += !str_eq(arc.metadata.name, meta.name);
    error += !str_eq(arc.metadata.author, meta.author);
    error += !str_eq(arc.metadata.version, meta.version);
    error += (arc.metadata.module_count != 2);
    for (u16 i = 0; i < 2 && i < arc.metadata.module_count; i++) {
        error += !str_eq(arc.metadata.modules[i], unicode_modules[i]);
    }

    if (error) {
        fprintf(stderr, "  unicode metadata mismatch (%d fields)\n", error);
    }

    return error;
}

static int test_corrupted_metadata(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_corrupted_meta.tlib");

    str test_mods[1];
    test_mods[0]          = str_init(alloc, "Foo");

    tl_tlib_metadata meta = {
      .name                   = str_init(alloc, "TestLib"),
      .author                 = str_init(alloc, "Author"),
      .version                = str_init(alloc, "1.0.0"),
      .modules                = test_mods,
      .module_count           = 1,
      .depends                = null,
      .depends_count          = 0,
      .depends_optional       = null,
      .depends_optional_count = 0,
    };

    tl_tlib_entry entry = {"test.tl", 7, (byte const *)"content", 7};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    // Truncate the file to corrupt it (CRC32 will not match)
    FILE *f = fopen(path, "r+b");
    if (!f) {
        fprintf(stderr, "  open for truncate failed\n");
        return 1;
    }

    // Truncate after magic(4) + version(4) + partial metadata = 12 bytes
    int trunc_result = ftruncate(fileno(f), 12);
    fclose(f);

    if (trunc_result != 0) {
        fprintf(stderr, "  truncate failed\n");
        return 1;
    }

    // Try to read - should fail gracefully
    tl_tlib_archive arc         = {0};
    int             read_result = tl_tlib_read(alloc, path, &arc);

    if (read_result == 0) {
        fprintf(stderr, "  read should have failed on corrupted metadata\n");
        return 1;
    }

    return 0;
}

static int test_crc32_integrity(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_crc32.tlib");

    tl_tlib_metadata meta  = make_test_metadata(alloc);
    tl_tlib_entry    entry = {"test.tl", 7, (byte const *)"content", 7};

    if (tl_tlib_write(alloc, path, &meta, &entry, 1)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    // Read the file, flip a byte in the metadata region, write it back
    FILE *f = fopen(path, "r+b");
    if (!f) {
        fprintf(stderr, "  open failed\n");
        return 1;
    }

    // Flip a byte at offset 10 (inside the name field)
    fseek(f, 10, SEEK_SET);
    byte b;
    if (fread(&b, 1, 1, f) != 1) {
        fclose(f);
        fprintf(stderr, "  read byte failed\n");
        return 1;
    }
    b ^= 0xFF;
    fseek(f, 10, SEEK_SET);
    if (fwrite(&b, 1, 1, f) != 1) {
        fclose(f);
        fprintf(stderr, "  write byte failed\n");
        return 1;
    }
    fclose(f);

    // Try to read - should fail with CRC mismatch
    tl_tlib_archive arc         = {0};
    int             read_result = tl_tlib_read(alloc, path, &arc);

    if (read_result == 0) {
        fprintf(stderr, "  read should have failed on CRC mismatch\n");
        return 1;
    }

    return 0;
}

// Helper to write a string to a file
static int write_file(char const *path, char const *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    size_t len = strlen(content);
    if (len != fwrite(content, 1, len, f)) {
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

static int test_pack_with_manifest(void) {
    allocator *alloc = default_allocator();

    // Create source file
    char src_path[512];
    make_temp_path(src_path, sizeof(src_path), "test_manifest_pack_lib.tl");
    char const *src_content = "#module Foo\nfoo() { 1 }\n";
    if (write_file(src_path, src_content)) {
        fprintf(stderr, "  failed to write source file\n");
        return 1;
    }

    // Create package.tl file
    char pkg_path[512];
    make_temp_path(pkg_path, sizeof(pkg_path), "test_manifest_pack_package.tl");
    char const *pkg_content = "format(1)\n"
                              "package(\"TestPkg\")\n"
                              "version(\"1.0.0\")\n"
                              "author(\"Tester\")\n"
                              "export(\"Foo\")\n"
                              "depend(\"Logger\", \"2.0.0\")\n"
                              "depend_optional(\"Debug\", \"0.1.0\")\n";
    if (write_file(pkg_path, pkg_content)) {
        fprintf(stderr, "  failed to write package.tl file\n");
        return 1;
    }

    // Parse package.tl
    tl_package pkg = {0};
    if (tl_package_parse_file(alloc, pkg_path, &pkg)) {
        fprintf(stderr, "  package.tl parse failed\n");
        return 1;
    }

    // Build pack opts from package (same logic as pack_files in tess_exe.c)
    tl_tlib_pack_opts opts = {.verbose = 0};
    opts.name              = str_cstr(&pkg.info.name);
    opts.version           = str_cstr(&pkg.info.version);
    opts.author = str_is_empty(pkg.info.author) ? null : str_cstr(&pkg.info.author);

    if (pkg.info.export_count > 0) {
        opts.modules      = pkg.info.exports;
        opts.module_count = (u16)pkg.info.export_count;
    }

    // Build depends
    if (pkg.dep_count > 0) {
        opts.depends       = alloc_malloc(alloc, pkg.dep_count * sizeof(str));
        opts.depends_count = (u16)pkg.dep_count;
        for (u32 i = 0; i < pkg.dep_count; i++) {
            opts.depends[i] = str_cat_3(alloc, pkg.deps[i].name, S("="), pkg.deps[i].version);
        }
    }

    // Build depends_optional
    if (pkg.optional_dep_count > 0) {
        opts.depends_optional       = alloc_malloc(alloc, pkg.optional_dep_count * sizeof(str));
        opts.depends_optional_count = (u16)pkg.optional_dep_count;
        for (u32 i = 0; i < pkg.optional_dep_count; i++) {
            opts.depends_optional[i] =
              str_cat_3(alloc, pkg.optional_deps[i].name, S("="), pkg.optional_deps[i].version);
        }
    }

    // Set up files array with the source file (normalized path)
    str       file_str = file_path_normalize(alloc, str_init(alloc, src_path));
    str       base_dir = file_dirname(alloc, file_str);
    str_sized files    = {.v = &file_str, .size = 1};

    // Create a minimal import resolver (no stdlib paths, so no files get filtered)
    import_resolver *resolver = import_resolver_create(alloc);

    // Pack
    char out_path[512];
    make_temp_path(out_path, sizeof(out_path), "test_manifest_pack.tlib");
    if (tl_tlib_pack(alloc, out_path, files, base_dir, resolver, opts)) {
        fprintf(stderr, "  pack failed\n");
        return 1;
    }

    // Read back and verify
    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, out_path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    int error = 0;

    // Verify metadata
    error += !str_eq(arc.metadata.name, S("TestPkg"));
    if (error) fprintf(stderr, "  name mismatch: %s\n", str_cstr(&arc.metadata.name));

    error += !str_eq(arc.metadata.version, S("1.0.0"));
    error += !str_eq(arc.metadata.author, S("Tester"));

    // Verify modules
    error += (arc.metadata.module_count != 1);
    if (arc.metadata.module_count >= 1) {
        error += !str_eq(arc.metadata.modules[0], S("Foo"));
    }

    // Verify depends
    error += (arc.metadata.depends_count != 1);
    if (arc.metadata.depends_count >= 1) {
        error += !str_eq(arc.metadata.depends[0], S("Logger=2.0.0"));
    }

    // Verify depends_optional
    error += (arc.metadata.depends_optional_count != 1);
    if (arc.metadata.depends_optional_count >= 1) {
        error += !str_eq(arc.metadata.depends_optional[0], S("Debug=0.1.0"));
    }

    // Verify file entries
    error += (arc.entries_count != 1);
    if (arc.entries_count >= 1) {
        error += (arc.entries[0].data_len != strlen(src_content));
        if (arc.entries[0].data_len == strlen(src_content)) {
            error += memcmp(arc.entries[0].data, src_content, strlen(src_content)) != 0;
        }
    }

    if (error) {
        fprintf(stderr, "  %d check(s) failed in test_pack_with_manifest\n", error);
    }

    return error;
}

// Create directory and parents (simplified version for tests).
static void test_mkdir_p(char const *path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            platform_mkdir(tmp);
            *p = '/';
        }
    }
    platform_mkdir(tmp);
}

// Helper: pack files from in-memory content using tl_tlib_pack.
// file_names and file_contents must have `count` elements.
// file_names are relative paths within a temp directory.
// Returns the result of tl_tlib_pack (0 = success).
static int pack_test_files(char const **file_names, char const **file_contents, u32 count) {
    allocator *alloc = default_allocator();

    // Write files to temp directory
    char base_path[512];
    make_temp_path(base_path, sizeof(base_path), "test_self_contain/");
    test_mkdir_p(base_path);

    str  base_dir   = str_init(alloc, base_path);

    str *file_paths = alloc_malloc(alloc, count * sizeof(str));
    for (u32 i = 0; i < count; i++) {
        str name      = str_init(alloc, file_names[i]);
        str full      = file_path_join(alloc, base_dir, name);
        file_paths[i] = full;

        // Create parent directory if needed
        str parent = file_dirname(alloc, full);
        if (!str_is_empty(parent)) {
            test_mkdir_p(str_cstr(&parent));
        }

        if (write_file(str_cstr(&full), file_contents[i])) {
            fprintf(stderr, "  pack_test_files: failed to write %s\n", str_cstr(&full));
            return 1;
        }
    }

    str_sized         files    = {.v = file_paths, .size = count};

    import_resolver  *resolver = import_resolver_create(alloc);
    tl_tlib_pack_opts opts     = {
          .verbose = 0,
          .name    = "TestSelfContain",
          .version = "1.0.0",
    };

    char out_path[512];
    make_temp_path(out_path, sizeof(out_path), "test_self_contain.tlib");

    int result = tl_tlib_pack(alloc, out_path, files, base_dir, resolver, opts);

    // Clean up temp files
    for (u32 i = 0; i < count; i++) {
        remove(str_cstr(&file_paths[i]));
    }

    return result;
}

static int test_pack_self_contained(void) {
    char const *names[]    = {"a.tl", "b.tl"};
    char const *contents[] = {"#import \"b.tl\"\nfoo() { 1 }\n", "bar() { 2 }\n"};
    int         result     = pack_test_files(names, contents, 2);
    if (result != 0) {
        fprintf(stderr, "  expected pack to succeed (self-contained)\n");
        return 1;
    }
    return 0;
}

static int test_pack_not_self_contained(void) {
    char const *names[]    = {"a.tl"};
    char const *contents[] = {"#import \"missing.tl\"\nfoo() { 1 }\n"};
    int         result     = pack_test_files(names, contents, 1);
    if (result == 0) {
        fprintf(stderr, "  expected pack to fail (missing import)\n");
        return 1;
    }
    return 0;
}

static int test_pack_stdlib_import_ok(void) {
    char const *names[]    = {"a.tl"};
    char const *contents[] = {"#import <stdio.tl>\nfoo() { 1 }\n"};
    int         result     = pack_test_files(names, contents, 1);
    if (result != 0) {
        fprintf(stderr, "  expected pack to succeed (stdlib import ignored)\n");
        return 1;
    }
    return 0;
}

static int test_pack_malformed_import_ok(void) {
    char const *names[]    = {"a.tl"};
    char const *contents[] = {"#import \"unterminated\nfoo() { 1 }\n"};
    int         result     = pack_test_files(names, contents, 1);
    if (result != 0) {
        fprintf(stderr, "  expected pack to succeed (malformed import skipped)\n");
        return 1;
    }
    return 0;
}

static int test_pack_subdir_import(void) {
    char const *names[]    = {"lib/a.tl", "util.tl"};
    char const *contents[] = {"#import \"../util.tl\"\nfoo() { 1 }\n", "bar() { 2 }\n"};
    int         result     = pack_test_files(names, contents, 2);
    if (result != 0) {
        fprintf(stderr, "  expected pack to succeed (subdir relative import resolves)\n");
        return 1;
    }
    return 0;
}

static int test_extract(void) {
    allocator *alloc = default_allocator();
    char       path[512];
    make_temp_path(path, sizeof(path), "test_tlib_extract.tlib");

    tl_tlib_metadata meta    = make_test_metadata(alloc);
    tl_tlib_entry    entries[2] = {
      {"lib.tl", 6, (byte const *)"#module Lib\nfoo() { 1 }\n", 24},
      {"sub/util.tl", 11, (byte const *)"#module Util\nbar() { 2 }\n", 25},
    };

    if (tl_tlib_write(alloc, path, &meta, entries, 2)) {
        fprintf(stderr, "  write failed\n");
        return 1;
    }

    tl_tlib_archive arc = {0};
    if (tl_tlib_read(alloc, path, &arc)) {
        fprintf(stderr, "  read failed\n");
        return 1;
    }

    // Extract to temp dir
    char extract_dir[512];
    make_temp_path(extract_dir, sizeof(extract_dir), "test_extract_out/");
    test_mkdir_p(extract_dir);

    str_array out_files = {.alloc = alloc};
    if (tl_tlib_extract(alloc, &arc, extract_dir, &out_files)) {
        fprintf(stderr, "  extract failed\n");
        return 1;
    }

    int error = 0;

    // Should have 2 extracted files
    error += (out_files.size != 2);
    if (error) {
        fprintf(stderr, "  expected 2 files, got %u\n", out_files.size);
        return error;
    }

    // Verify files exist and have correct content
    for (u32 i = 0; i < out_files.size; i++) {
        char *data;
        u32   size;
        file_read(alloc, str_cstr(&out_files.v[i]), &data, &size);
        if (!data) {
            fprintf(stderr, "  failed to read extracted file: %s\n", str_cstr(&out_files.v[i]));
            error++;
            continue;
        }
        if (size != entries[i].data_len || memcmp(data, entries[i].data, size) != 0) {
            fprintf(stderr, "  content mismatch for file %u\n", i);
            error++;
        }
    }

    if (error) {
        fprintf(stderr, "  %d check(s) failed in test_extract\n", error);
    }

    return error;
}

// ===========================================================================
// End-to-end integration tests (Phase 7: package consumption)
// ===========================================================================
//
// These tests exercise the full pipeline: create library, pack to .tlib,
// create consumer with package.tl, compile with tess exe, run the result.
// They require the `tess` binary to be built (it is when running via `make test`).

static char e2e_project_root[512];
static char e2e_tess_exe[512];
static char e2e_stdlib_dir[512];

static void init_e2e_paths(void) {
    char buf[512];
    span s = {.buf = buf, .len = sizeof(buf)};
    char *cwd = file_current_working_directory(s);
    if (cwd) {
        snprintf(e2e_project_root, sizeof(e2e_project_root), "%s", cwd);
    }
#ifdef MOS_WINDOWS
    snprintf(e2e_tess_exe, sizeof(e2e_tess_exe), "%s\\tess.exe", e2e_project_root);
    snprintf(e2e_stdlib_dir, sizeof(e2e_stdlib_dir), "%s\\src\\tl\\std", e2e_project_root);
#else
    snprintf(e2e_tess_exe, sizeof(e2e_tess_exe), "%s/tess", e2e_project_root);
    snprintf(e2e_stdlib_dir, sizeof(e2e_stdlib_dir), "%s/src/tl/std", e2e_project_root);
#endif
}

static int copy_file(char const *src, char const *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return 1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return 1; }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { fclose(in); fclose(out); return 1; }
    }
    fclose(in);
    fclose(out);
    return 0;
}

static int run_cmd(char const *cmd) {
    int ret = system(cmd);
#ifdef MOS_WINDOWS
    return ret;
#else
    if (WIFEXITED(ret)) return WEXITSTATUS(ret);
    return -1;
#endif
}

// Test: pack a simple library, consume it via package.tl, compile, run.
// Library has one module (Greeter) with greet() returning 42.
// Consumer calls Greeter.greet() from main() and returns the result.
static int test_e2e_basic_package(void) {
    // -- Set up library project --
    char lib_dir[512];
    make_temp_path(lib_dir, sizeof(lib_dir), "e2e_basic_lib/");
    test_mkdir_p(lib_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", lib_dir);
    if (write_file(path, "format(1)\npackage(\"Greeter\")\nversion(\"1.0.0\")\nexport(\"Greeter\")\n")) {
        fprintf(stderr, "  failed to write lib package.tl\n");
        return 1;
    }

    snprintf(path, sizeof(path), "%sgreeter.tl", lib_dir);
    if (write_file(path, "#module Greeter\n\ngreet() { 42 }\n")) {
        fprintf(stderr, "  failed to write greeter.tl\n");
        return 1;
    }

    // -- Pack library --
    char tlib_path[512];
    snprintf(tlib_path, sizeof(tlib_path), "%sGreeter.tlib", lib_dir);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" greeter.tl -o Greeter.tlib 2>&1",
             lib_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack failed\n");
        return 1;
    }

    // -- Set up consumer project --
    char app_dir[512], libs_dir[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_basic_app/");
    snprintf(libs_dir, sizeof(libs_dir), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(libs_dir);

    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    if (write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"Greeter\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n")) {
        fprintf(stderr, "  failed to write app package.tl\n");
        return 1;
    }

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    if (write_file(path, "#module main\n\nmain() {\n  Greeter.greet()\n}\n")) {
        fprintf(stderr, "  failed to write main.tl\n");
        return 1;
    }

    // Copy .tlib to consumer's libs/
    char dst_tlib[512];
    snprintf(dst_tlib, sizeof(dst_tlib), "%sGreeter.tlib", libs_dir);
    if (copy_file(tlib_path, dst_tlib)) {
        fprintf(stderr, "  failed to copy Greeter.tlib\n");
        return 1;
    }

    // -- Compile consumer --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess exe failed\n");
        return 1;
    }

    // -- Run and verify --
    int exit_code = run_cmd(out_exe);
    if (exit_code != 42) {
        fprintf(stderr, "  expected exit code 42, got %d\n", exit_code);
        return 1;
    }

    return 0;
}

// Test: version mismatch between package.tl depend() and .tlib metadata.
// Consumer expects version 2.0.0 but .tlib has 1.0.0 → compilation must fail.
static int test_e2e_version_mismatch(void) {
    // -- Set up library (version 1.0.0) --
    char lib_dir[512];
    make_temp_path(lib_dir, sizeof(lib_dir), "e2e_vermis_lib/");
    test_mkdir_p(lib_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", lib_dir);
    write_file(path, "format(1)\npackage(\"Greeter\")\nversion(\"1.0.0\")\nexport(\"Greeter\")\n");

    snprintf(path, sizeof(path), "%sgreeter.tl", lib_dir);
    write_file(path, "#module Greeter\n\ngreet() { 42 }\n");

    char tlib_path[512];
    snprintf(tlib_path, sizeof(tlib_path), "%sGreeter.tlib", lib_dir);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" greeter.tl -o Greeter.tlib 2>&1",
             lib_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack failed\n");
        return 1;
    }

    // -- Set up consumer expecting version 2.0.0 --
    char app_dir[512], libs_dir[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_vermis_app/");
    snprintf(libs_dir, sizeof(libs_dir), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(libs_dir);

    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"Greeter\", \"2.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() { Greeter.greet() }\n");

    char dst_tlib[512];
    snprintf(dst_tlib, sizeof(dst_tlib), "%sGreeter.tlib", libs_dir);
    copy_file(tlib_path, dst_tlib);

    // -- Compile consumer: should fail due to version mismatch --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) == 0) {
        fprintf(stderr, "  tess exe should have failed (version mismatch)\n");
        return 1;
    }

    return 0;
}

// Test: dependency .tlib not found in any depend_path().
// Consumer depends on "NonExistent" which has no .tlib → compilation must fail.
static int test_e2e_dep_not_found(void) {
    char app_dir[512], libs_dir[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_notfound_app/");
    snprintf(libs_dir, sizeof(libs_dir), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(libs_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"NonExistent\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() { 0 }\n");

    char cmd[2048], out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) == 0) {
        fprintf(stderr, "  tess exe should have failed (package not found)\n");
        return 1;
    }

    return 0;
}

// Test: library with multiple files and internal imports using nested modules.
// Library has MathLib (public, imports internal.tl) and MathLib.Internal (nested).
// Consumer uses MathLib.add() which internally calls MathLib.Internal.check().
static int test_e2e_multi_file_library(void) {
    // -- Set up library --
    char lib_dir[512];
    make_temp_path(lib_dir, sizeof(lib_dir), "e2e_multi_lib/");
    test_mkdir_p(lib_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", lib_dir);
    write_file(path,
            "format(1)\n"
            "package(\"MathLib\")\n"
            "version(\"1.0.0\")\n"
            "export(\"MathLib\")\n");

    snprintf(path, sizeof(path), "%smath.tl", lib_dir);
    write_file(path,
            "#module MathLib\n"
            "#import \"internal.tl\"\n"
            "\n"
            "add(a, b) {\n"
            "  MathLib.Internal.check(a)\n"
            "  MathLib.Internal.check(b)\n"
            "  a + b\n"
            "}\n");

    snprintf(path, sizeof(path), "%sinternal.tl", lib_dir);
    write_file(path,
            "#module MathLib.Internal\n"
            "\n"
            "check(x) {\n"
            "  if x < 0 { 0 - x }\n"
            "  else { x }\n"
            "}\n");

    // -- Pack library (passing root file, imports are resolved automatically) --
    char tlib_path[512];
    snprintf(tlib_path, sizeof(tlib_path), "%sMathLib.tlib", lib_dir);

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" math.tl -o MathLib.tlib 2>&1",
             lib_dir, e2e_tess_exe, e2e_stdlib_dir);
    // internal.tl is resolved automatically via #import in math.tl
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack failed\n");
        return 1;
    }

    // -- Set up consumer --
    char app_dir[512], libs_dir[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_multi_app/");
    snprintf(libs_dir, sizeof(libs_dir), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(libs_dir);

    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"MathLib\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path,
            "#module main\n"
            "\n"
            "main() {\n"
            "  result := MathLib.add(17, 25)\n"
            "  result\n"
            "}\n");

    char dst_tlib[512];
    snprintf(dst_tlib, sizeof(dst_tlib), "%sMathLib.tlib", libs_dir);
    copy_file(tlib_path, dst_tlib);

    // -- Compile and run --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess exe failed\n");
        return 1;
    }

    int exit_code = run_cmd(out_exe);
    if (exit_code != 42) {
        fprintf(stderr, "  expected exit code 42, got %d\n", exit_code);
        return 1;
    }

    return 0;
}

// Phase 8: Transitive dependency chain A -> B -> C
static int test_e2e_transitive_deps(void) {
    // -- Create LogLib (module Logger) --
    char loglib_dir[512];
    make_temp_path(loglib_dir, sizeof(loglib_dir), "e2e_trans_loglib/");
    test_mkdir_p(loglib_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", loglib_dir);
    write_file(path, "format(1)\npackage(\"LogLib\")\nversion(\"1.0.0\")\nexport(\"Logger\")\n");

    snprintf(path, sizeof(path), "%slogger.tl", loglib_dir);
    write_file(path, "#module Logger\n\nlog_value() { 10 }\n");

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" logger.tl -o LogLib.tlib 2>&1",
             loglib_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack LogLib failed\n");
        return 1;
    }

    // -- Create MathLib (module MathLib, depends on LogLib) --
    char mathlib_dir[512], mathlib_libs[512];
    make_temp_path(mathlib_dir, sizeof(mathlib_dir), "e2e_trans_mathlib/");
    snprintf(mathlib_libs, sizeof(mathlib_libs), "%slibs/", mathlib_dir);
    test_mkdir_p(mathlib_dir);
    test_mkdir_p(mathlib_libs);

    snprintf(path, sizeof(path), "%spackage.tl", mathlib_dir);
    write_file(path,
            "format(1)\n"
            "package(\"MathLib\")\n"
            "version(\"2.0.0\")\n"
            "export(\"MathLib\")\n"
            "depend(\"LogLib\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smath.tl", mathlib_dir);
    write_file(path,
            "#module MathLib\n\n"
            "compute() {\n"
            "  Logger.log_value() + 32\n"
            "}\n");

    // Copy LogLib.tlib to MathLib's libs/
    char src_tlib[512], dst_tlib[512];
    snprintf(src_tlib, sizeof(src_tlib), "%sLogLib.tlib", loglib_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sLogLib.tlib", mathlib_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" math.tl -o MathLib.tlib 2>&1",
             mathlib_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack MathLib failed\n");
        return 1;
    }

    // -- Create consumer App --
    char app_dir[512], app_libs[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_trans_app/");
    snprintf(app_libs, sizeof(app_libs), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(app_libs);

    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"MathLib\", \"2.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() {\n  MathLib.compute()\n}\n");

    // Copy both .tlibs to App's libs/
    snprintf(src_tlib, sizeof(src_tlib), "%sMathLib.tlib", mathlib_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sMathLib.tlib", app_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(src_tlib, sizeof(src_tlib), "%sLogLib.tlib", loglib_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sLogLib.tlib", app_libs);
    copy_file(src_tlib, dst_tlib);

    // -- Compile and run --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess exe failed\n");
        return 1;
    }

    int exit_code = run_cmd(out_exe);
    if (exit_code != 42) {
        fprintf(stderr, "  expected exit code 42, got %d\n", exit_code);
        return 1;
    }

    return 0;
}

// Phase 8: Diamond dependency (LibA and LibB both depend on BaseLib)
static int test_e2e_diamond_deps(void) {
    // -- Create BaseLib --
    char base_dir[512];
    make_temp_path(base_dir, sizeof(base_dir), "e2e_diamond_base/");
    test_mkdir_p(base_dir);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", base_dir);
    write_file(path, "format(1)\npackage(\"BaseLib\")\nversion(\"1.0.0\")\nexport(\"Base\")\n");

    snprintf(path, sizeof(path), "%sbase.tl", base_dir);
    write_file(path, "#module Base\n\nval() { 20 }\n");

    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" base.tl -o BaseLib.tlib 2>&1",
             base_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack BaseLib failed\n");
        return 1;
    }

    // -- Create LibA (depends on BaseLib) --
    char liba_dir[512], liba_libs[512];
    make_temp_path(liba_dir, sizeof(liba_dir), "e2e_diamond_liba/");
    snprintf(liba_libs, sizeof(liba_libs), "%slibs/", liba_dir);
    test_mkdir_p(liba_dir);
    test_mkdir_p(liba_libs);

    snprintf(path, sizeof(path), "%spackage.tl", liba_dir);
    write_file(path,
            "format(1)\n"
            "package(\"LibA\")\n"
            "version(\"1.0.0\")\n"
            "export(\"ModA\")\n"
            "depend(\"BaseLib\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smoda.tl", liba_dir);
    write_file(path, "#module ModA\n\ncompute() { Base.val() + 1 }\n");

    char src_tlib[512], dst_tlib[512];
    snprintf(src_tlib, sizeof(src_tlib), "%sBaseLib.tlib", base_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sBaseLib.tlib", liba_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" moda.tl -o LibA.tlib 2>&1",
             liba_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack LibA failed\n");
        return 1;
    }

    // -- Create LibB (depends on BaseLib) --
    char libb_dir[512], libb_libs[512];
    make_temp_path(libb_dir, sizeof(libb_dir), "e2e_diamond_libb/");
    snprintf(libb_libs, sizeof(libb_libs), "%slibs/", libb_dir);
    test_mkdir_p(libb_dir);
    test_mkdir_p(libb_libs);

    snprintf(path, sizeof(path), "%spackage.tl", libb_dir);
    write_file(path,
            "format(1)\n"
            "package(\"LibB\")\n"
            "version(\"1.0.0\")\n"
            "export(\"ModB\")\n"
            "depend(\"BaseLib\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smodb.tl", libb_dir);
    write_file(path, "#module ModB\n\ncompute() { Base.val() + 1 }\n");

    snprintf(dst_tlib, sizeof(dst_tlib), "%sBaseLib.tlib", libb_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" pack --no-standard-includes -S \"%s\" modb.tl -o LibB.tlib 2>&1",
             libb_dir, e2e_tess_exe, e2e_stdlib_dir);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess pack LibB failed\n");
        return 1;
    }

    // -- Create App depending on both LibA and LibB --
    char app_dir[512], app_libs[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_diamond_app/");
    snprintf(app_libs, sizeof(app_libs), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(app_libs);

    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"LibA\", \"1.0.0\")\n"
            "depend(\"LibB\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path,
            "#module main\n\n"
            "main() {\n"
            "  ModA.compute() + ModB.compute()\n"
            "}\n");

    // Copy all .tlibs to App's libs/
    snprintf(src_tlib, sizeof(src_tlib), "%sLibA.tlib", liba_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sLibA.tlib", app_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(src_tlib, sizeof(src_tlib), "%sLibB.tlib", libb_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sLibB.tlib", app_libs);
    copy_file(src_tlib, dst_tlib);

    snprintf(src_tlib, sizeof(src_tlib), "%sBaseLib.tlib", base_dir);
    snprintf(dst_tlib, sizeof(dst_tlib), "%sBaseLib.tlib", app_libs);
    copy_file(src_tlib, dst_tlib);

    // -- Compile and run --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) != 0) {
        fprintf(stderr, "  tess exe failed\n");
        return 1;
    }

    int exit_code = run_cmd(out_exe);
    if (exit_code != 42) {
        fprintf(stderr, "  expected exit code 42, got %d\n", exit_code);
        return 1;
    }

    return 0;
}

// Phase 8: Circular dependency detection (A -> B -> A)
static int test_e2e_circular_deps(void) {
    allocator *alloc = default_allocator();

    // Create two .tlib files that reference each other via raw write API
    char libs_dir[512];
    make_temp_path(libs_dir, sizeof(libs_dir), "e2e_circular_libs/");
    test_mkdir_p(libs_dir);

    // A.tlib depends on B=1.0.0
    str a_depends[1];
    a_depends[0] = str_init(alloc, "PkgB=1.0.0");

    str a_modules[1];
    a_modules[0] = str_init(alloc, "ModA");

    tl_tlib_metadata meta_a = {
        .name           = str_init(alloc, "PkgA"),
        .author         = str_empty(),
        .version        = str_init(alloc, "1.0.0"),
        .modules        = a_modules,
        .module_count   = 1,
        .depends        = a_depends,
        .depends_count  = 1,
    };

    char const *a_src = "#module ModA\n\nfoo() { 1 }\n";
    tl_tlib_entry a_entry = {"moda.tl", 7, (byte const *)a_src, (u32)strlen(a_src)};

    char a_path[512];
    snprintf(a_path, sizeof(a_path), "%sPkgA.tlib", libs_dir);
    if (tl_tlib_write(alloc, a_path, &meta_a, &a_entry, 1)) {
        fprintf(stderr, "  failed to write PkgA.tlib\n");
        return 1;
    }

    // B.tlib depends on A=1.0.0
    str b_depends[1];
    b_depends[0] = str_init(alloc, "PkgA=1.0.0");

    str b_modules[1];
    b_modules[0] = str_init(alloc, "ModB");

    tl_tlib_metadata meta_b = {
        .name           = str_init(alloc, "PkgB"),
        .author         = str_empty(),
        .version        = str_init(alloc, "1.0.0"),
        .modules        = b_modules,
        .module_count   = 1,
        .depends        = b_depends,
        .depends_count  = 1,
    };

    char const *b_src = "#module ModB\n\nbar() { 2 }\n";
    tl_tlib_entry b_entry = {"modb.tl", 7, (byte const *)b_src, (u32)strlen(b_src)};

    char b_path[512];
    snprintf(b_path, sizeof(b_path), "%sPkgB.tlib", libs_dir);
    if (tl_tlib_write(alloc, b_path, &meta_b, &b_entry, 1)) {
        fprintf(stderr, "  failed to write PkgB.tlib\n");
        return 1;
    }

    // -- Create App depending on PkgA --
    char app_dir[512], app_libs[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_circular_app/");
    snprintf(app_libs, sizeof(app_libs), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(app_libs);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"PkgA\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() { 0 }\n");

    // Copy .tlibs to App's libs/
    char dst[512];
    snprintf(dst, sizeof(dst), "%sPkgA.tlib", app_libs);
    copy_file(a_path, dst);
    snprintf(dst, sizeof(dst), "%sPkgB.tlib", app_libs);
    copy_file(b_path, dst);

    // -- Compile: should fail with circular dependency error --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) == 0) {
        fprintf(stderr, "  tess exe should have failed (circular dependency)\n");
        return 1;
    }

    return 0;
}

// Phase 8: Version conflict in diamond (LibA needs Base=1.0.0, LibB needs Base=2.0.0)
static int test_e2e_version_conflict(void) {
    allocator *alloc = default_allocator();

    char libs_dir[512];
    make_temp_path(libs_dir, sizeof(libs_dir), "e2e_conflict_libs/");
    test_mkdir_p(libs_dir);

    // BaseLib v1.0.0
    str base_modules[1];
    base_modules[0] = str_init(alloc, "Base");

    tl_tlib_metadata meta_base = {
        .name         = str_init(alloc, "BaseLib"),
        .author       = str_empty(),
        .version      = str_init(alloc, "1.0.0"),
        .modules      = base_modules,
        .module_count = 1,
    };

    char const *base_src = "#module Base\n\nval() { 1 }\n";
    tl_tlib_entry base_entry = {"base.tl", 7, (byte const *)base_src, (u32)strlen(base_src)};

    char base_path[512];
    snprintf(base_path, sizeof(base_path), "%sBaseLib.tlib", libs_dir);
    tl_tlib_write(alloc, base_path, &meta_base, &base_entry, 1);

    // LibA depends on BaseLib=1.0.0
    str a_depends[1];
    a_depends[0] = str_init(alloc, "BaseLib=1.0.0");

    str a_modules[1];
    a_modules[0] = str_init(alloc, "ModA");

    tl_tlib_metadata meta_a = {
        .name          = str_init(alloc, "LibA"),
        .author        = str_empty(),
        .version       = str_init(alloc, "1.0.0"),
        .modules       = a_modules,
        .module_count  = 1,
        .depends       = a_depends,
        .depends_count = 1,
    };

    char const *a_src = "#module ModA\n\nfoo() { Base.val() }\n";
    tl_tlib_entry a_entry = {"moda.tl", 7, (byte const *)a_src, (u32)strlen(a_src)};

    char a_path[512];
    snprintf(a_path, sizeof(a_path), "%sLibA.tlib", libs_dir);
    tl_tlib_write(alloc, a_path, &meta_a, &a_entry, 1);

    // LibB depends on BaseLib=2.0.0 (conflict!)
    str b_depends[1];
    b_depends[0] = str_init(alloc, "BaseLib=2.0.0");

    str b_modules[1];
    b_modules[0] = str_init(alloc, "ModB");

    tl_tlib_metadata meta_b = {
        .name          = str_init(alloc, "LibB"),
        .author        = str_empty(),
        .version       = str_init(alloc, "1.0.0"),
        .modules       = b_modules,
        .module_count  = 1,
        .depends       = b_depends,
        .depends_count = 1,
    };

    char const *b_src = "#module ModB\n\nbar() { Base.val() }\n";
    tl_tlib_entry b_entry = {"modb.tl", 7, (byte const *)b_src, (u32)strlen(b_src)};

    char b_path[512];
    snprintf(b_path, sizeof(b_path), "%sLibB.tlib", libs_dir);
    tl_tlib_write(alloc, b_path, &meta_b, &b_entry, 1);

    // -- Create App depending on LibA and LibB --
    char app_dir[512], app_libs[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_conflict_app/");
    snprintf(app_libs, sizeof(app_libs), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(app_libs);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"LibA\", \"1.0.0\")\n"
            "depend(\"LibB\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() { 0 }\n");

    // Copy all .tlibs
    char dst[512];
    snprintf(dst, sizeof(dst), "%sLibA.tlib", app_libs);
    copy_file(a_path, dst);
    snprintf(dst, sizeof(dst), "%sLibB.tlib", app_libs);
    copy_file(b_path, dst);
    snprintf(dst, sizeof(dst), "%sBaseLib.tlib", app_libs);
    copy_file(base_path, dst);

    // -- Compile: should fail (LibA needs Base=1.0.0, LibB needs Base=2.0.0) --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) == 0) {
        fprintf(stderr, "  tess exe should have failed (version conflict)\n");
        return 1;
    }

    return 0;
}

// Phase 8: Missing transitive dependency
static int test_e2e_missing_transitive_dep(void) {
    allocator *alloc = default_allocator();

    // Create MathLib that depends on LogLib=1.0.0 (via raw write)
    char libs_dir[512];
    make_temp_path(libs_dir, sizeof(libs_dir), "e2e_missing_trans_libs/");
    test_mkdir_p(libs_dir);

    str m_depends[1];
    m_depends[0] = str_init(alloc, "LogLib=1.0.0");

    str m_modules[1];
    m_modules[0] = str_init(alloc, "MathLib");

    tl_tlib_metadata meta_m = {
        .name          = str_init(alloc, "MathLib"),
        .author        = str_empty(),
        .version       = str_init(alloc, "1.0.0"),
        .modules       = m_modules,
        .module_count  = 1,
        .depends       = m_depends,
        .depends_count = 1,
    };

    char const *m_src = "#module MathLib\n\nadd(a, b) { a + b }\n";
    tl_tlib_entry m_entry = {"math.tl", 7, (byte const *)m_src, (u32)strlen(m_src)};

    char m_path[512];
    snprintf(m_path, sizeof(m_path), "%sMathLib.tlib", libs_dir);
    tl_tlib_write(alloc, m_path, &meta_m, &m_entry, 1);

    // -- Create App depending on MathLib, but LogLib is NOT available --
    char app_dir[512], app_libs[512];
    make_temp_path(app_dir, sizeof(app_dir), "e2e_missing_trans_app/");
    snprintf(app_libs, sizeof(app_libs), "%slibs/", app_dir);
    test_mkdir_p(app_dir);
    test_mkdir_p(app_libs);

    char path[512];
    snprintf(path, sizeof(path), "%spackage.tl", app_dir);
    write_file(path,
            "format(1)\n"
            "package(\"App\")\n"
            "version(\"0.1.0\")\n"
            "depend(\"MathLib\", \"1.0.0\")\n"
            "depend_path(\"./libs\")\n");

    snprintf(path, sizeof(path), "%smain.tl", app_dir);
    write_file(path, "#module main\n\nmain() { 0 }\n");

    // Copy only MathLib.tlib (NOT LogLib.tlib)
    char dst[512];
    snprintf(dst, sizeof(dst), "%sMathLib.tlib", app_libs);
    copy_file(m_path, dst);

    // -- Compile: should fail with missing transitive dep --
    char out_exe[512];
    snprintf(out_exe, sizeof(out_exe), "%sapp", app_dir);
    char cmd[2048];
    snprintf(cmd, sizeof(cmd),
             "cd \"%s\" && \"%s\" exe --no-standard-includes -S \"%s\" -o \"%s\" main.tl 2>&1",
             app_dir, e2e_tess_exe, e2e_stdlib_dir, out_exe);
    if (run_cmd(cmd) == 0) {
        fprintf(stderr, "  tess exe should have failed (missing transitive dep)\n");
        return 1;
    }

    return 0;
}

int main(void) {
    init_temp_dir();
    init_e2e_paths();

    int error      = 0;
    int this_error = 0;
    T(test_roundtrip)
    T(test_empty_archive)
    T(test_filename_validation)
    T(test_byte_order)
    T(test_large_payload)
    T(test_metadata_roundtrip)
    T(test_metadata_empty_fields)
    T(test_metadata_unicode)
    T(test_corrupted_metadata)
    T(test_crc32_integrity)
    T(test_pack_with_manifest)
    T(test_pack_self_contained)
    T(test_pack_not_self_contained)
    T(test_pack_stdlib_import_ok)
    T(test_pack_malformed_import_ok)
    T(test_pack_subdir_import)
    T(test_extract)
    T(test_e2e_basic_package)
    T(test_e2e_version_mismatch)
    T(test_e2e_dep_not_found)
    T(test_e2e_multi_file_library)
    T(test_e2e_transitive_deps)
    T(test_e2e_diamond_deps)
    T(test_e2e_circular_deps)
    T(test_e2e_version_conflict)
    T(test_e2e_missing_transitive_dep)
    return error;
}
