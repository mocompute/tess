#include "tlib.h"
#include "file.h"
#include "import_resolver.h"
#include "manifest.h"
#include "platform.h"

#include <stdio.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <io.h>
#define ftruncate(fd, size) _chsize(fd, size)
#define fileno _fileno
#else
#include <unistd.h>
#endif

#define T(name)                                    \
	this_error = name();                       \
	if (this_error) {                          \
		fprintf(stderr, "FAILED: %s\n", #name); \
		error += this_error;               \
	}

// Platform-specific temp directory
static char temp_dir[512];

static void init_temp_dir(void) {
#ifdef MOS_WINDOWS
	char temp[MAX_PATH];
	GetTempPathA(MAX_PATH, temp);
	snprintf(temp_dir, sizeof(temp_dir), "%s", temp);
#else
	snprintf(temp_dir, sizeof(temp_dir), "/tmp/");
#endif
}

static void make_temp_path(char *buf, size_t bufsize, char const *filename) {
	snprintf(buf, bufsize, "%s%s", temp_dir, filename);
}

static str test_modules[2];

static tl_tlib_metadata make_test_metadata(allocator *alloc) {
	test_modules[0] = str_init(alloc, "Foo");
	test_modules[1] = str_init(alloc, "Bar");
	return (tl_tlib_metadata){
		.name                    = str_init(alloc, "TestLib"),
		.author                  = str_init(alloc, "Tester"),
		.version                 = str_init(alloc, "1.0.0"),
		.modules                 = test_modules,
		.module_count            = 2,
		.requires                = null,
		.requires_count          = 0,
		.requires_optional       = null,
		.requires_optional_count = 0,
	};
}

static int test_roundtrip(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_roundtrip.tlib");

	tl_tlib_metadata meta = make_test_metadata(alloc);

	tl_tlib_entry entries[3] = {
		{ "hello.tl",     8, (byte const *)"hello!\n",    7 },
		{ "sub/world.tl", 12, (byte const *)"world!\n",   7 },
		{ "empty.tl",     8, (byte const *)"",            0 },
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
		if (arc.entries[i].name_len != entries[i].name_len
		    || memcmp(arc.entries[i].name, entries[i].name, entries[i].name_len) != 0) {
			fprintf(stderr, "  name mismatch at entry %u\n", i);
			return 1;
		}
		if (arc.entries[i].data_len != entries[i].data_len
		    || memcmp(arc.entries[i].data, entries[i].data, entries[i].data_len) != 0) {
			fprintf(stderr, "  data mismatch at entry %u\n", i);
			return 1;
		}
	}

	return 0;
}

static int test_empty_archive(void) {
	allocator *alloc = default_allocator();
	char path[512];
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
	error += tl_tlib_valid_filename("..x", 3) != 1;

	/* invalid names */
	error += tl_tlib_valid_filename("", 0) != 0;
	error += tl_tlib_valid_filename("/etc/passwd", 11) != 0;
	error += tl_tlib_valid_filename("../foo", 6) != 0;
	error += tl_tlib_valid_filename("foo/../bar", 10) != 0;
	error += tl_tlib_valid_filename("..", 2) != 0;
	error += tl_tlib_valid_filename("foo\\bar", 7) != 0;
	error += tl_tlib_valid_filename("C:\\foo", 6) != 0;
	error += tl_tlib_valid_filename("D:/bar.tl", 9) != 0;

	if (error)
		fprintf(stderr, "  %d filename validation check(s) failed\n", error);

	return error;
}

static int test_byte_order(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_byteorder.tlib");

	tl_tlib_metadata meta = make_test_metadata(alloc);
	tl_tlib_entry entry = { "a.tl", 4, (byte const *)"x", 1 };

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
		fprintf(stderr, "  magic mismatch: expected TLIB, got %c%c%c%c\n",
		        header[0], header[1], header[2], header[3]);
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
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_large.tlib");

	tl_tlib_metadata meta = make_test_metadata(alloc);

	u32 size = 1024 * 1024;
	byte *data = alloc_malloc(alloc, size);
	for (u32 i = 0; i < size; i++)
		data[i] = (byte)(i * 7 + 13);

	tl_tlib_entry entry = { "big.tl", 6, data, size };

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
	if (arc.entries_count != 1 || arc.entries[0].data_len != size
	    || memcmp(arc.entries[0].data, data, size) != 0) {
		fprintf(stderr, "  large payload data mismatch\n");
		result = 1;
	}

	alloc_free(alloc, data);
	return result;
}

static int test_metadata_roundtrip(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_metadata.tlib");

	str modules[3];
	modules[0] = str_init(alloc, "Core");
	modules[1] = str_init(alloc, "Utils");
	modules[2] = str_init(alloc, "Helper");

	str requires[1];
	requires[0] = str_init(alloc, "Lib=1.0.0");

	str requires_opt[1];
	requires_opt[0] = str_init(alloc, "Debug=0.5.0");

	tl_tlib_metadata meta = {
		.name                    = str_init(alloc, "MetaTest"),
		.author                  = str_init(alloc, "Alice"),
		.version                 = str_init(alloc, "2.3.4"),
		.modules                 = modules,
		.module_count            = 3,
		.requires                = requires,
		.requires_count          = 1,
		.requires_optional       = requires_opt,
		.requires_optional_count = 1,
	};

	tl_tlib_entry entry = { "test.tl", 7, (byte const *)"content", 7 };

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
	error += (arc.metadata.requires_count != 1);
	if (arc.metadata.requires_count >= 1) {
		error += !str_eq(arc.metadata.requires[0], requires[0]);
	}
	error += (arc.metadata.requires_optional_count != 1);
	if (arc.metadata.requires_optional_count >= 1) {
		error += !str_eq(arc.metadata.requires_optional[0], requires_opt[0]);
	}

	if (error) {
		fprintf(stderr, "  metadata mismatch (%d fields)\n", error);
	}

	return error;
}

static int test_metadata_empty_fields(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_metadata_empty.tlib");

	tl_tlib_metadata meta = {
		.name                    = str_init(alloc, "MinimalLib"),
		.author                  = str_empty(),
		.version                 = str_init(alloc, "0.1"),
		.modules                 = null,
		.module_count            = 0,
		.requires                = null,
		.requires_count          = 0,
		.requires_optional       = null,
		.requires_optional_count = 0,
	};

	tl_tlib_entry entry = { "a.tl", 4, (byte const *)"x", 1 };

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
	error += (arc.metadata.requires_count != 0);
	error += (arc.metadata.requires_optional_count != 0);

	if (error) {
		fprintf(stderr, "  empty field handling failed (%d errors)\n", error);
	}

	return error;
}

static int test_metadata_unicode(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_metadata_unicode.tlib");

	str unicode_modules[2];
	unicode_modules[0] = str_init(alloc, "Módulo");
	unicode_modules[1] = str_init(alloc, "Функция");

	tl_tlib_metadata meta = {
		.name                    = str_init(alloc, "Bibliothèque"),
		.author                  = str_init(alloc, "日本語 Author™"),
		.version                 = str_init(alloc, "1.0.0-β"),
		.modules                 = unicode_modules,
		.module_count            = 2,
		.requires                = null,
		.requires_count          = 0,
		.requires_optional       = null,
		.requires_optional_count = 0,
	};

	tl_tlib_entry entry = { "test.tl", 7, (byte const *)"content", 7 };

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
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_corrupted_meta.tlib");

	str test_mods[1];
	test_mods[0] = str_init(alloc, "Foo");

	tl_tlib_metadata meta = {
		.name                    = str_init(alloc, "TestLib"),
		.author                  = str_init(alloc, "Author"),
		.version                 = str_init(alloc, "1.0.0"),
		.modules                 = test_mods,
		.module_count            = 1,
		.requires                = null,
		.requires_count          = 0,
		.requires_optional       = null,
		.requires_optional_count = 0,
	};

	tl_tlib_entry entry = { "test.tl", 7, (byte const *)"content", 7 };

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
	tl_tlib_archive arc = {0};
	int read_result = tl_tlib_read(alloc, path, &arc);

	if (read_result == 0) {
		fprintf(stderr, "  read should have failed on corrupted metadata\n");
		return 1;
	}

	return 0;
}

static int test_crc32_integrity(void) {
	allocator *alloc = default_allocator();
	char path[512];
	make_temp_path(path, sizeof(path), "test_tlib_crc32.tlib");

	tl_tlib_metadata meta = make_test_metadata(alloc);
	tl_tlib_entry entry = { "test.tl", 7, (byte const *)"content", 7 };

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
	tl_tlib_archive arc = {0};
	int read_result = tl_tlib_read(alloc, path, &arc);

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

	// Create manifest file
	char manifest_path[512];
	make_temp_path(manifest_path, sizeof(manifest_path), "test_manifest_pack.toml");
	char const *manifest_content =
		"[package]\n"
		"name = TestPkg\n"
		"version = 1.0.0\n"
		"author = Tester\n"
		"modules = [Foo]\n"
		"\n"
		"[depend.Logger]\n"
		"version = 2.0.0\n"
		"\n"
		"[depend-optional.Debug]\n"
		"version = 0.1.0\n";
	if (write_file(manifest_path, manifest_content)) {
		fprintf(stderr, "  failed to write manifest file\n");
		return 1;
	}

	// Parse manifest
	tl_manifest manifest = {0};
	if (tl_manifest_parse_file(alloc, manifest_path, &manifest)) {
		fprintf(stderr, "  manifest parse failed\n");
		return 1;
	}

	// Build pack opts from manifest (same logic as pack_files in tess_exe.c)
	tl_tlib_pack_opts opts = {.verbose = 0};
	opts.name    = str_cstr(&manifest.package.name);
	opts.version = str_cstr(&manifest.package.version);
	opts.author  = str_is_empty(manifest.package.author) ? null : str_cstr(&manifest.package.author);

	if (manifest.package.module_count > 0) {
		opts.modules      = manifest.package.modules;
		opts.module_count = (u16)manifest.package.module_count;
	}

	// Build requires
	if (manifest.dep_count > 0) {
		opts.requires       = alloc_malloc(alloc, manifest.dep_count * sizeof(str));
		opts.requires_count = (u16)manifest.dep_count;
		for (u32 i = 0; i < manifest.dep_count; i++) {
			opts.requires[i] =
				str_cat_3(alloc, manifest.deps[i].name, S("="), manifest.deps[i].version);
		}
	}

	// Build requires_optional
	if (manifest.optional_dep_count > 0) {
		opts.requires_optional       = alloc_malloc(alloc, manifest.optional_dep_count * sizeof(str));
		opts.requires_optional_count = (u16)manifest.optional_dep_count;
		for (u32 i = 0; i < manifest.optional_dep_count; i++) {
			opts.requires_optional[i] = str_cat_3(alloc, manifest.optional_deps[i].name, S("="),
			                                      manifest.optional_deps[i].version);
		}
	}

	// Set up files array with the source file (normalized path)
	str file_str  = file_path_normalize(alloc, str_init(alloc, src_path));
	str base_dir  = file_dirname(alloc, file_str);
	str_sized files = {.v = &file_str, .size = 1};

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

	// Verify requires
	error += (arc.metadata.requires_count != 1);
	if (arc.metadata.requires_count >= 1) {
		error += !str_eq(arc.metadata.requires[0], S("Logger=2.0.0"));
	}

	// Verify requires_optional
	error += (arc.metadata.requires_optional_count != 1);
	if (arc.metadata.requires_optional_count >= 1) {
		error += !str_eq(arc.metadata.requires_optional[0], S("Debug=0.1.0"));
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

int main(void) {
	init_temp_dir();

	int error = 0;
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
	return error;
}
