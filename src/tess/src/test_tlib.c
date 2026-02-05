#include "tlib.h"

#include <stdio.h>
#include <string.h>

#define T(name)                                    \
	this_error = name();                       \
	if (this_error) {                          \
		fprintf(stderr, "FAILED: %s\n", #name); \
		error += this_error;               \
	}

static int test_roundtrip(void) {
	allocator *alloc = default_allocator();
	char const *path = "/tmp/test_tlib_roundtrip.tlib";

	tl_tlib_entry entries[3] = {
		{ "hello.tl",     8, (byte const *)"hello!\n",    7 },
		{ "sub/world.tl", 12, (byte const *)"world!\n",   7 },
		{ "empty.tl",     8, (byte const *)"",            0 },
	};

	if (tl_tlib_write(alloc, path, entries, 3)) {
		fprintf(stderr, "  write failed\n");
		return 1;
	}

	tl_tlib_archive arc = {0};
	if (tl_tlib_read(alloc, path, &arc)) {
		fprintf(stderr, "  read failed\n");
		return 1;
	}

	if (arc.count != 3) {
		fprintf(stderr, "  expected 3 entries, got %u\n", arc.count);
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
	char const *path = "/tmp/test_tlib_empty.tlib";

	if (tl_tlib_write(alloc, path, null, 0)) {
		fprintf(stderr, "  write failed\n");
		return 1;
	}

	tl_tlib_archive arc = {0};
	if (tl_tlib_read(alloc, path, &arc)) {
		fprintf(stderr, "  read failed\n");
		return 1;
	}

	if (arc.count != 0) {
		fprintf(stderr, "  expected 0 entries, got %u\n", arc.count);
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
	char const *path = "/tmp/test_tlib_byteorder.tlib";

	tl_tlib_entry entry = { "a.tl", 4, (byte const *)"x", 1 };

	if (tl_tlib_write(alloc, path, &entry, 1)) {
		fprintf(stderr, "  write failed\n");
		return 1;
	}

	// Read raw bytes and verify header is big-endian
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "  open failed\n");
		return 1;
	}

	byte header[16];
	if (fread(header, 1, 16, f) != 16) {
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
	char const *path = "/tmp/test_tlib_large.tlib";

	u32 size = 1024 * 1024;
	byte *data = alloc_malloc(alloc, size);
	for (u32 i = 0; i < size; i++)
		data[i] = (byte)(i * 7 + 13);

	tl_tlib_entry entry = { "big.tl", 6, data, size };

	if (tl_tlib_write(alloc, path, &entry, 1)) {
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
	if (arc.count != 1 || arc.entries[0].data_len != size
	    || memcmp(arc.entries[0].data, data, size) != 0) {
		fprintf(stderr, "  large payload data mismatch\n");
		result = 1;
	}

	alloc_free(alloc, data);
	return result;
}

int main(void) {
	int error = 0;
	int this_error = 0;
	T(test_roundtrip)
	T(test_empty_archive)
	T(test_filename_validation)
	T(test_byte_order)
	T(test_large_payload)
	return error;
}
