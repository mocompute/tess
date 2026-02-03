#include "tlib.h"
#include "file.h"
#include "libdeflate.h"

#include <stdio.h>
#include <string.h>

#define TLIB_MAGIC         0x42494C54u /* "TLIB" little-endian */
#define TLIB_VERSION       1u
#define TLIB_HEADER_SIZE   16u
#define TLIB_MAX_FILE_SIZE (64u * 1024u * 1024u)

static inline void write_u32_le(byte *p, u32 v) {
	p[0] = (byte)(v);
	p[1] = (byte)(v >> 8);
	p[2] = (byte)(v >> 16);
	p[3] = (byte)(v >> 24);
}

static inline u32 read_u32_le(byte const *p) {
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

int tl_tlib_valid_filename(char const *name, u32 len) {
	if (len == 0)
		return 0;
	if (name[0] == '/')
		return 0;
	/* reject Windows absolute paths (e.g. C:\, D:/) */
	if (len >= 2 && name[1] == ':')
		return 0;
	for (u32 i = 0; i < len; i++) {
		if (name[i] == '\\')
			return 0;
	}
	/* reject ".." as a path component */
	if (len == 2 && name[0] == '.' && name[1] == '.')
		return 0;
	for (u32 i = 0; i + 2 < len; i++) {
		if (name[i] == '/' && name[i + 1] == '.' && name[i + 2] == '.') {
			if (i + 3 == len || name[i + 3] == '/')
				return 0;
		}
	}
	if (len >= 3 && name[0] == '.' && name[1] == '.' && name[2] == '/')
		return 0;
	return 1;
}

int tl_tlib_write(allocator *alloc, char const *output_path,
                  tl_tlib_entry const *entries, u32 count) {
	/* compute payload size */
	u32 payload_size = 4; /* file count */
	for (u32 i = 0; i < count; i++) {
		payload_size += 4 + entries[i].name_len + 4 + entries[i].data_len;
	}

	/* serialize payload */
	byte *payload = alloc_malloc(alloc, payload_size);
	byte *p = payload;
	write_u32_le(p, count);
	p += 4;
	for (u32 i = 0; i < count; i++) {
		write_u32_le(p, entries[i].name_len);
		p += 4;
		memcpy(p, entries[i].name, entries[i].name_len);
		p += entries[i].name_len;
		write_u32_le(p, entries[i].data_len);
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

	size_t bound = libdeflate_deflate_compress_bound(c, payload_size);
	byte *compressed = alloc_malloc(alloc, bound);

	size_t compressed_size = libdeflate_deflate_compress(
		c, payload, payload_size, compressed, bound);
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
	write_u32_le(header + 0, TLIB_MAGIC);
	write_u32_le(header + 4, TLIB_VERSION);
	write_u32_le(header + 8, payload_size);
	write_u32_le(header + 12, (u32)compressed_size);

	int ok = fwrite(header, 1, TLIB_HEADER_SIZE, f) == TLIB_HEADER_SIZE
	      && fwrite(compressed, 1, compressed_size, f) == compressed_size;

	fclose(f);
	alloc_free(alloc, compressed);

	if (!ok) {
		fprintf(stderr, "tlib: failed to write output file\n");
		return 1;
	}
	return 0;
}

int tl_tlib_read(allocator *alloc, char const *input_path,
                 tl_tlib_archive *out) {
	out->entries = null;
	out->count = 0;

	char *raw = null;
	u32 raw_size = 0;
	file_read(alloc, input_path, &raw, &raw_size);
	if (!raw)
		return 1;

	if (raw_size < TLIB_HEADER_SIZE) {
		fprintf(stderr, "tlib: file too small\n");
		alloc_free(alloc, raw);
		return 1;
	}

	byte const *h = (byte const *)raw;
	u32 magic           = read_u32_le(h + 0);
	u32 version         = read_u32_le(h + 4);
	u32 uncompressed_sz = read_u32_le(h + 8);
	u32 compressed_sz   = read_u32_le(h + 12);

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
	byte *payload = alloc_malloc(alloc, uncompressed_sz);

	struct libdeflate_decompressor *d = libdeflate_alloc_decompressor();
	if (!d) {
		alloc_free(alloc, payload);
		alloc_free(alloc, raw);
		fprintf(stderr, "tlib: failed to allocate decompressor\n");
		return 1;
	}

	size_t actual_out = 0;
	enum libdeflate_result r = libdeflate_deflate_decompress(
		d, raw + TLIB_HEADER_SIZE, compressed_sz,
		payload, uncompressed_sz, &actual_out);
	libdeflate_free_decompressor(d);
	alloc_free(alloc, raw);

	if (r != LIBDEFLATE_SUCCESS || actual_out != uncompressed_sz) {
		fprintf(stderr, "tlib: decompression failed\n");
		alloc_free(alloc, payload);
		return 1;
	}

	/* parse entries */
	byte const *p = payload;
	byte const *end = payload + uncompressed_sz;

	if (p + 4 > end) goto corrupt;
	u32 count = read_u32_le(p);
	p += 4;

	tl_tlib_entry *entries = alloc_calloc(alloc, count, sizeof(tl_tlib_entry));

	for (u32 i = 0; i < count; i++) {
		if (p + 4 > end) goto corrupt_entries;
		u32 name_len = read_u32_le(p);
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
		u32 data_len = read_u32_le(p);
		p += 4;
		if (p + data_len > end) goto corrupt_entries;
		byte const *data = p;
		p += data_len;

		entries[i].name = name;
		entries[i].name_len = name_len;
		entries[i].data = data;
		entries[i].data_len = data_len;
	}

	out->entries = entries;
	out->count = count;
	/* caller owns both entries and payload (entries point into payload) */
	return 0;

corrupt_entries:
	alloc_free(alloc, entries);
corrupt:
	fprintf(stderr, "tlib: corrupted payload\n");
	alloc_free(alloc, payload);
	return 1;
}
