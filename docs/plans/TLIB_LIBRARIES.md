# Tess Library Packing: Design Plan

## Summary

Add a `tess pack` command that bundles Tess source files into a `.tlib` archive for distribution as a reusable library. Consumers import `.tlib` files via the existing `#import` mechanism, and the compiler performs whole-program compilation as usual. The `[[export]]` attribute controls which symbols are part of the library's public API.

This is distinct from C-compatible shared libraries (`tess lib` producing `.so`/`.dll`), which remain unchanged. A future `[[c_export]]` attribute may be introduced for C-compatible symbol export.

---

## Design Decisions

### Source-Only Archives

A `.tlib` is a compressed archive of `.tl` source files. There is no pre-compilation, no IR, no metadata format. The consumer extracts the source and compiles everything together via whole-program compilation.

**Rationale:**
- The compiler is designed for whole-program compilation; this preserves that model
- Generic functions specialize correctly because the consumer sees the full source
- No metadata format to design, version, or keep in sync
- No partial linking or symbol resolution complexity

### Single-File Distribution

Source is compressed and stored inside the `.tlib` file itself rather than as sidecar files. This makes distribution ergonomic (one file to ship) and keeps source somewhat obscured compared to shipping raw `.tl` files.

### `[[export]]` as API Boundary

- `[[export]]` marks a symbol as part of the library's public API
- `[[export]]` is ignored by `tess pack` (it just bundles everything)
- `[[export]]` is ignored by `tess exe` on standalone files
- `[[export]]` is enforced when `tess exe` compiles a program that imports a `.tlib`: consumer code may only reference `[[export]]`-annotated symbols from the library
- A future `[[c_export(name)]]` variant may allow custom symbol naming for C-compatible libraries

### Standard Library Exclusion

Standard library files are excluded from `.tlib` archives. The consumer's compiler has its own standard library. Only user-authored source files are bundled.

---

## `.tlib` Archive Format

A custom binary format, chosen over tar for simplicity, security, and zero external dependencies (aside from libdeflate for compression).

### Layout

```
[4 bytes] Magic: "TLIB"
[4 bytes] Version: 1 (little-endian uint32)
[4 bytes] Uncompressed payload size (little-endian uint32)
[4 bytes] Compressed payload size (little-endian uint32)
[N bytes] Deflate-compressed payload
```

### Payload (before compression)

```
[4 bytes] File count (little-endian uint32)
For each file:
    [4 bytes] Filename length (little-endian uint32)
    [N bytes] Filename (UTF-8, relative path)
    [4 bytes] Content length (little-endian uint32)
    [N bytes] Content (raw source bytes)
```

### Path Handling

Files are stored with their directory structure relative to a common root (the nearest common ancestor of all packed files). This preserves import resolution semantics:

- `liba/core.tl` and `libb/core.tl` can coexist
- `#import "../util/util.tl"` from `liba/foo.tl` resolves correctly to `util/util.tl`
- The compiler's existing relative import resolution works unchanged

`..` components are allowed in stored paths. The validation rule is:
- No path may escape the archive root (e.g., `../../../etc/passwd` is rejected)
- After packing, every `#import` in every archived file must resolve to another file in the archive (self-containment check)

### Safety

- All lengths are bounds-checked against remaining buffer size on read
- Maximum individual file size and total archive size limits prevent memory exhaustion
- Filenames are validated: no absolute paths, no escape from archive root

### Compression

Deflate via vendored libdeflate. Only the payload is compressed; the 16-byte header is uncompressed so the reader can allocate the decompression buffer before decompressing.

---

## Implementation Phases

### Phase 1: Vendor libdeflate

- Vendor libdeflate source into `vendor/libdeflate/`
- Integrate into both Makefile and CMakeLists.txt
- Only the compressor and decompressor are needed

### Phase 2: `.tlib` Archive Format + Reader/Writer

Implement a small module (likely in `src/tess/`):

- `tlib_write(allocator, files[], count, output_path) -> int`
  - Serializes file entries into the payload format
  - Deflate-compresses the payload
  - Writes header + compressed payload to output file

- `tlib_read(allocator, input_path) -> tlib_archive`
  - Reads and validates the header
  - Decompresses the payload
  - Parses file entries with bounds checking
  - Returns array of (filename, content) pairs

Unit tests for write/read roundtrip.

### Phase 3: `tess pack` Command

Add a new subcommand to `tess_exe.c`:

1. Parse the input `.tl` file(s)
2. Resolve the full import graph (reuse existing `files_in_order()`)
3. Exclude standard library files
4. Read all resolved `.tl` source files
5. Write them into a `.tlib` via the Phase 2 writer

No compilation or type checking occurs. This is purely source collection and archiving.

**CLI:**
```bash
tess pack foo.tl -o Foo.tlib
tess pack foo.tl bar.tl -o Foo.tlib    # multiple entry points
```

### Phase 4: `.tlib` Consumption in `#import` Resolution

Extend import resolution in `tess_exe.c`:

1. When `find_import_file()` searches for an import, also look for `<Name>.tlib` in the search paths
2. When a `.tlib` is found, extract its source files into arena-allocated buffers
3. Feed the extracted `.tl` files into the existing compilation pipeline as regular source files
4. The rest of the compiler (parsing, inference, transpilation) works unchanged

**Consumer CLI:**
```bash
tess exe main.tl -L /path/to/libs -o main    # -L adds library search paths
```

**Consumer source:**
```
#import Foo    // finds Foo.tlib in search paths
```

### Phase 5: `[[export]]` Enforcement

When compiling a program that imports a `.tlib`:

1. After parsing the library source, identify all symbols with `[[export]]` attributes
2. During type inference, track which library-defined symbols are referenced by consumer code
3. Error if consumer code references a non-`[[export]]` symbol from the library
4. Library-internal symbols remain usable within the library's own source files

### Phase 6: Tests

- Unit test: `.tlib` write/read roundtrip (correct format, corruption detection)
- Unit test: filename validation rejects `..` and absolute paths
- Integration test: `tess pack` a multi-file library, then `tess exe` a consumer that imports it
- Integration test: `[[export]]` enforcement — error when accessing non-exported symbol
- Integration test: generics in a `.tlib` specialize correctly in the consumer
- Add all tests to both Makefile and CMakeLists.txt

---

## Open Questions

- **Library search path convention**: Should there be a default search path (e.g., `~/.tess/lib/`) in addition to `-L` flags?
- **Version conflicts**: What happens if two `.tlib` files contain the same module? First-found wins, or error?
- **Transitive dependencies**: If `Foo.tlib` was packed from source that imported `Bar`, should `Bar`'s source be included in `Foo.tlib`, or should the consumer independently provide `Bar.tlib`?
- **`[[export]]` on types**: Should `[[export]]` work on struct/enum definitions, or only on functions and values?
