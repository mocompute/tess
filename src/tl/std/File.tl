// File -- Streaming file I/O, convenience read/write, and path utilities.
//
// Wraps C stdio for cross-platform file access. All operations use binary
// mode to avoid Windows CR/LF translation. Error handling via Result[T, IOError].
//
// Usage:
//   #import <File.tl>
//   content := try File.read_file("data.txt")
//   try File.write_file("out.txt", content)
#module File
#import <Alloc.tl>
#import <Array.tl>
#import <String.tl>
#import <cstdlib.tl>
#import <cstdio.tl>
#import <cstring.tl>

Allocator = Alloc.Allocator

// Shorthand for string literal to String conversion.
_S(s) { String.from_cstr(s) }


// -- C FFI --

// Helpers that need C logic (platform branching, multi-step operations).
#ifc
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#undef small
#include <io.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

// File metadata — need C logic for platform branching.
static int tl_file_exists(const char *path) {
#ifdef _WIN32
    return _access(path, 0) == 0 ? 1 : 0;
#else
    struct stat st;
    return stat(path, &st) == 0 ? 1 : 0;
#endif
}

static int tl_file_is_dir(const char *path) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

static long long tl_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long sz = ftell(f);
    fclose(f);
    return (long long)sz;
}

static int tl_file_mkdir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

// Directory scanning
typedef struct {
    char **paths;
    size_t count;
    size_t capacity;
} tl_path_list;

static void tl_path_list_init(tl_path_list *l) {
    l->paths = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void tl_path_list_push(tl_path_list *l, const char *path) {
    if (l->count == l->capacity) {
        size_t new_cap = l->capacity == 0 ? 16 : l->capacity * 2;
        char **new_paths = (char**)realloc(l->paths, new_cap * sizeof(char*));
        if (!new_paths) return;
        l->paths = new_paths;
        l->capacity = new_cap;
    }
    size_t len = strlen(path);
    char *copy = (char*)malloc(len + 1);
    if (!copy) return;
    memcpy(copy, path, len + 1);
    l->paths[l->count++] = copy;
}

static void tl_path_list_free(tl_path_list *l) {
    for (size_t i = 0; i < l->count; i++) free(l->paths[i]);
    free(l->paths);
    l->paths = NULL;
    l->count = 0;
    l->capacity = 0;
}

static void tl_scan_dir_impl(const char *dir, const char *ext, tl_path_list *out, int depth) {
    if (depth > 64) return;
#ifdef _WIN32
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            tl_scan_dir_impl(path, ext, out, depth + 1);
        } else {
            if (!ext) {
                tl_path_list_push(out, path);
            } else {
                size_t nl = strlen(fd.cFileName);
                size_t el = strlen(ext);
                if (nl >= el && strcmp(fd.cFileName + nl - el, ext) == 0) {
                    tl_path_list_push(out, path);
                }
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            tl_scan_dir_impl(path, ext, out, depth + 1);
        } else {
            if (!ext) {
                tl_path_list_push(out, path);
            } else {
                size_t nl = strlen(entry->d_name);
                size_t el = strlen(ext);
                if (nl >= el && strcmp(entry->d_name + nl - el, ext) == 0) {
                    tl_path_list_push(out, path);
                }
            }
        }
    }
    closedir(d);
#endif
}

// Temp directory (with trailing separator)
static int tl_file_tmpdir(char *buf, size_t size) {
#ifdef _WIN32
    DWORD len = GetTempPathA((DWORD)size, buf);
    return (len > 0 && len < size) ? 0 : -1;
#else
    const char *t = getenv("TMPDIR");
    if (!t || !t[0]) t = "/tmp";
    int r = snprintf(buf, size, "%s/", t);
    return (r > 0 && (size_t)r < size) ? 0 : -1;
#endif
}

// CWD for path resolution
static int tl_file_getcwd(char *buf, size_t size) {
#ifdef _WIN32
    return _getcwd(buf, (int)size) != NULL ? 0 : -1;
#else
    return getcwd(buf, size) != NULL ? 0 : -1;
#endif
}

static char tl_file_separator(void) {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}
#endc

c_tl_file_exists   (path: CString)                -> CInt
c_tl_file_is_dir   (path: CString)                -> CInt
c_tl_file_size     (path: CString)                -> Int
c_tl_file_mkdir    (path: CString)                -> CInt
c_tl_file_tmpdir   (buf: Ptr[CChar], size: CSize) -> CInt
c_tl_file_getcwd   (buf: Ptr[CChar], size: CSize) -> CInt
c_tl_file_separator()                             -> CChar


// -- Types --

Mode: | Read                            // "rb"
      | Write                           // "wb"
      | Append                          // "ab"
      | ReadWrite                       // "r+b"
      | WriteRead                       // "w+b"
      | AppendRead                      // "a+b"

IOError: | NotFound         { path: String }
         | PermissionDenied { path: String                                    }
         | AlreadyExists    { path: String                                    }
         | IOFailed         { path: String, message: String, errno_code: CInt }

SeekFrom: | Start | Current | End

File: {
    _handle: Ptr[any],
    _alloc:  Ptr[Allocator],
    _path:   String,
}


// -- Synopsis --

// Handle lifecycle.
open  (alloc: Ptr[Allocator], path: String, mode: Mode) -> Result[Ptr[File], IOError]
open  (path: String, mode: Mode)                        -> Result[Ptr[File], IOError]
close (self: Ptr[File])                                 -> Void

// Streaming read.
read       (self: Ptr[File], buf: Ptr[Byte], count: CSize) -> Result[CSize, IOError]
read_line  (self: Ptr[File], alloc: Ptr[Allocator])        -> Result[Option[String], IOError]
read_line  (self: Ptr[File])                               -> Result[Option[String], IOError]
read_all   (self: Ptr[File], alloc: Ptr[Allocator])        -> Result[String, IOError]
read_all   (self: Ptr[File])                               -> Result[String, IOError]
read_bytes (self: Ptr[File], alloc: Ptr[Allocator])        -> Result[Array[Byte], IOError]
read_bytes (self: Ptr[File])                               -> Result[Array[Byte], IOError]

// Streaming write.
write       (self: Ptr[File], buf: Ptr[Const[Byte]], count: CSize) -> Result[CSize, IOError]
write_str   (self: Ptr[File], data: String)                        -> Result[Void, IOError]
write_bytes (self: Ptr[File], data: Array[Byte])                   -> Result[Void, IOError]

// Positioning.
seek   (self: Ptr[File], offset: Int, from: SeekFrom) -> Result[Void, IOError]
tell   (self: Ptr[File])                              -> Result[Int, IOError]
rewind (self: Ptr[File])                              -> Void

// Query.
path   (self: Ptr[Const[File]]) -> String
is_eof (self: Ptr[Const[File]]) -> Bool

// Convenience (no handle needed).
read_file       (alloc: Ptr[Allocator], path: String) -> Result[String, IOError]
read_file       (path: String)                        -> Result[String, IOError]
read_file_bytes (alloc: Ptr[Allocator], path: String) -> Result[Array[Byte], IOError]
read_file_bytes (path: String)                        -> Result[Array[Byte], IOError]
write_file      (path: String, content: String)       -> Result[Void, IOError]
write_file_bytes(path: String, data: Array[Byte])     -> Result[Void, IOError]
append_file     (path: String, content: String)       -> Result[Void, IOError]
read_lines      (alloc: Ptr[Allocator], path: String) -> Result[Array[String], IOError]
read_lines      (path: String)                        -> Result[Array[String], IOError]

// File metadata.
exists       (path: String) -> Bool
is_directory (path: String) -> Bool
size         (path: String) -> Result[Int, IOError]
temp_dir     ()             -> String

// Directory operations.
create_dir(path: String)                                             -> Result[Void, IOError]
scan_dir  (alloc: Ptr[Allocator], path: String, ext: Option[String]) -> Result[Array[String], IOError]
scan_dir  (path: String, ext: Option[String])                        -> Result[Array[String], IOError]


// -- Private helpers --

_mode_str       (mode: Mode)      -> CString
_errno_to_error (path: String)    -> IOError
_remaining_bytes(self: Ptr[File]) -> Result[CSize, IOError]


// ============================================================
// Path submodule — pure path string manipulation
// ============================================================

#module File.Path

Allocator = Alloc.Allocator

_S(s) { String.from_cstr(s) }

// -- Synopsis --

// Components.
dirname  (alloc: Ptr[Allocator], path: String) -> String
dirname  (path: String)                        -> String
basename (path: String)                        -> String
extension(path: String)                        -> Option[String]
stem     (path: String)                        -> String

// Manipulation.
join     (alloc: Ptr[Allocator], dir: String, file: String)         -> String
join     (dir: String, file: String)                                -> String
normalize(alloc: Ptr[Allocator], path: String)                      -> String
normalize(path: String)                                             -> String
relative (alloc: Ptr[Allocator], from_dir: String, to_path: String) -> Result[String, File.IOError]
relative (from_dir: String, to_path: String)                        -> Result[String, File.IOError]

// Queries.
is_absolute(path: String) -> Bool
is_relative(path: String) -> Bool
separator  ()             -> CChar


// -- Private helpers --

_is_sep (c: CChar) -> Bool


// -- Implementation --

_is_sep(c) {
    c == '/' || c == '\\'
}

dirname(alloc, path) {
    n := String.len(path)
    if n == 0 { return _S(".") }

    // Strip trailing separators (but not the root "/")
    end := n
    while end > 1 {
        ch: Some := String.char_at(path, end - 1) else { break }
        if !_is_sep(ch.v) { break }
        end -= 1
    }

    // Find last separator in remaining
    i: CSize := end
    while i > 0 {
        i -= 1
        ch: Some := String.char_at(path, i) else { continue }
        if _is_sep(ch.v) {
            if i == 0 { return String.from_bytes(alloc, "/", 1) }
            return String.slice(alloc, path, 0, i)
        }
    }
    _S(".")
}

dirname(path) {
    dirname(Alloc.context.default, path)
}

basename(path) {
    n := String.len(path)
    if n == 0 { return String.empty() }

    // Strip trailing separators
    end := n
    while end > 0 {
        ch: Some := String.char_at(path, end - 1) else { break }
        if !_is_sep(ch.v) { break }
        end -= 1
    }

    if end == 0 { return String.from_bytes("/", 1) }

    // Find last separator before end
    i: CSize := end
    while i > 0 {
        i -= 1
        ch: Some := String.char_at(path, i) else { continue }
        if _is_sep(ch.v) {
            return String.slice(path, i + 1, end)
        }
    }
    String.slice(path, 0, end)
}

extension(path) {
    base := basename(path)
    n    := String.len(base)
    if n == 0 { return None }

    dot: Some := String.last_index_of_char(base, '.') else { return None }
    if dot.v == 0 { return None }       // ".hidden" — dot at start is not an extension
    Some(String.slice(base, dot.v, n))
}

stem(path) {
    base := basename(path)
    n    := String.len(base)
    if n == 0 { return String.empty() }

    dot: Some := String.last_index_of_char(base, '.') else { return base }
    if dot.v == 0 { return base }       // ".hidden" — the whole thing is the stem
    String.slice(base, 0, dot.v)
}

join(alloc, dir, file) {
    // If file is absolute, return file
    if is_absolute(file) { return String.copy(alloc, file) }

    dn := String.len(dir)
    if dn == 0 { return String.copy(alloc, file) }

    fn := String.len(file)
    if fn == 0 { return String.copy(alloc, dir) }

    // Check if dir ends with separator
    last_ch: Some := String.char_at(dir, dn - 1) else { return String.copy(alloc, file) }
    if _is_sep(last_ch.v) {
        return String.cat(alloc, dir, file)
    }

    sep := String.from_bytes("/", 1)
    String.cat_3(alloc, dir, sep, file)
}

join(dir, file) {
    join(Alloc.context.default, dir, file)
}

normalize(alloc, path) {
    n := String.len(path)
    if n == 0 { return String.empty() }

    first_ch: Some := String.char_at(path, 0) else { return String.empty() }
    abs            := _is_sep(first_ch.v)

    // Split path into components
    parts := String.split(alloc, path, _S("/"))

    // Also handle backslash-separated paths
    result   := Array.empty[String]()
    i: CSize := 0
    while i < parts.size, i += 1 {
        sub      := String.split(alloc, parts.v.[i], _S("\\"))
        j: CSize := 0
        while j < sub.size, j += 1 {
            Array.push(result.&, alloc, sub.v.[j])
        }
    }

    // Process components
    stack    := Array.empty[String]()
    k: CSize := 0
    while k < result.size, k += 1 {
        part := result.v.[k]
        pn   := String.len(part)

        if pn == 0 || String.eq(part, _S(".")) {
            continue
        }

        if String.eq(part, _S("..")) {
            if stack.size > 0 && !String.eq(stack.v.[stack.size - 1], _S("..")) {
                Array.pop(stack.&)
                void
            } else if !abs {
                Array.push(stack.&, alloc, part)
            }
            continue
        }

        Array.push(stack.&, alloc, part)
    }

    if stack.size == 0 {
        if abs { return _S("/") }
        return _S(".")
    }

    out := String.join(alloc, _S("/"), stack)
    if abs {
        return String.cat(alloc, _S("/"), out)
    }
    out
}

normalize(path) {
    normalize(Alloc.context.default, path)
}

relative(alloc, from_dir, to_path) {
    if String.is_empty(from_dir) || String.is_empty(to_path) {
        return Err(File.IOFailed(path = String.empty(),
                                 message    = _S("relative: empty input"),
                                 errno_code = 0))
    }

    // Normalize both paths; make absolute if relative
    norm_from := normalize(alloc, from_dir)
    norm_to   := normalize(alloc, to_path)

    if !is_absolute(norm_from) {
        cwd_buf: CArray[CChar, 4096] := void
        cwd_ptr: Ptr[CChar]          := cwd_buf
        if c_tl_file_getcwd(cwd_ptr, 4096) != 0 {
            return Err(File.IOFailed(path = from_dir,
                                     message    = _S("relative: getcwd failed"),
                                     errno_code = c_errno))
        }
        cwd := String.from_cstr(alloc, cwd_ptr)
        norm_from = join(alloc, cwd, norm_from)
        norm_from = normalize(alloc, norm_from)
    }

    if !is_absolute(norm_to) {
        cwd_buf: CArray[CChar, 4096] := void
        cwd_ptr: Ptr[CChar]          := cwd_buf
        if c_tl_file_getcwd(cwd_ptr, 4096) != 0 {
            return Err(File.IOFailed(path = to_path,
                                     message    = _S("relative: getcwd failed"),
                                     errno_code = c_errno))
        }
        cwd := String.from_cstr(alloc, cwd_ptr)
        norm_to = join(alloc, cwd, norm_to)
        norm_to = normalize(alloc, norm_to)
    }

    // Split into components
    from_parts := String.split(alloc, norm_from, _S("/"))
    to_parts   := String.split(alloc, norm_to, _S("/"))

    // Find common prefix length
    common: CSize := 0
    max_common    := if from_parts.size < to_parts.size { from_parts.size } else { to_parts.size }
    while common < max_common {
        if !String.eq(from_parts.v.[common], to_parts.v.[common]) { break }
        common += 1
    }

    // Build relative path: ".." for each remaining from_part, then remaining to_parts
    result     := Array.empty[String]()
    ups: CSize := from_parts.size - common
    u:   CSize   := 0
    while u < ups, u += 1 {
        Array.push(result.&, alloc, _S(".."))
    }

    d: CSize := common
    while d < to_parts.size, d += 1 {
        Array.push(result.&, alloc, to_parts.v.[d])
    }

    if result.size == 0 { return Ok(_S(".")) }
    Ok(String.join(alloc, _S("/"), result))
}

relative(from_dir, to_path) {
    relative(Alloc.context.default, from_dir, to_path)
}

is_absolute(path) {
    n := String.len(path)
    if n == 0 { return false }
    first: Some := String.char_at(path, 0) else { return false }
    if _is_sep(first.v) { return true }
    // Windows drive letter: C:\ or C:/
    if n < 3 { return false }
    second: Some := String.char_at(path, 1) else { return false }
    third:  Some  := String.char_at(path, 2) else { return false }
    second.v == ':' && _is_sep(third.v)
}

is_relative(path) {
    !is_absolute(path)
}

separator() {
    c_tl_file_separator()
}


// ============================================================
// Back to File module for implementation
// ============================================================

#module File


// -- Private helpers --

_mode_str(mode) {
    when mode {
        r:  Read { "rb" }
        w:  Write { "wb" }
        a:  Append { "ab" }
        rw: ReadWrite { "r+b" }
        wr: WriteRead { "w+b" }
        ar: AppendRead { "a+b" }
    }
}

_errno_to_error(path) {
    code := c_errno
    msg  := String.from_cstr(c_strerror(code))

    if code == c_ENOENT { return NotFound(path = path) }
    if code == c_EACCES { return PermissionDenied(path = path) }
    if code == c_EEXIST { return AlreadyExists(path = path) }

    IOFailed(path = path, message = msg, errno_code = code)
}

_remaining_bytes(self) {
    cur_l := c_ftell(self->_handle)
    if cur_l < 0 { return Err(_errno_to_error(self->_path)) }

    if c_fseek(self->_handle, 0, c_SEEK_END) != 0 {
        return Err(_errno_to_error(self->_path))
    }
    end_l := c_ftell(self->_handle)
    if end_l < 0 { return Err(_errno_to_error(self->_path)) }

    if c_fseek(self->_handle, cur_l, c_SEEK_SET) != 0 {
        return Err(_errno_to_error(self->_path))
    }

    remaining: CSize := end_l - cur_l
    Ok(remaining)
}


// -- Handle lifecycle --

open(alloc, path, mode) {
    mode_cstr := _mode_str(mode)
    path_cstr := String.cstr(path.&)

    handle := c_fopen(path_cstr, mode_cstr)
    if handle == null { return Err(_errno_to_error(path)) }

    f: Ptr[File] := alloc->malloc(alloc, sizeof[File]())
    if f == null { _tl_fatal_("File.open: out of memory") }
    f->_handle = handle
    f->_alloc  = alloc
    f->_path   = path
    Ok(f)
}

open(path, mode) {
    open(Alloc.context.default, path, mode)
}

close(self) {
    if self->_handle != null {
        c_fclose(self->_handle)
        self->_handle = null
    }
    alloc := self->_alloc
    alloc->free(alloc, self)
}


// -- Streaming read --

read(self, buf, count) {
    n := c_fread(buf, 1, count, self->_handle)
    if n < count && c_ferror(self->_handle) != 0 {
        return Err(_errno_to_error(self->_path))
    }
    Ok(n)
}

read_line(self, alloc) {
    // Check for EOF at start
    ch := c_fgetc(self->_handle)
    if ch == c_EOF {
        if c_ferror(self->_handle) != 0 {
            return Err(_errno_to_error(self->_path))
        }
        return Ok(None)
    }

    // Read characters into a growable buffer
    buf:      CArray[CChar, 256] := void
    buf_ptr:  Ptr[CChar]     := buf
    len:      CSize              := 0
    cap:      CSize              := 256
    heap_buf: Ptr[CChar]    := null

    while ch != c_EOF {
        c: CChar := ch
        if c == '\n' { break }

        if len == cap {
            new_cap             := cap * 2
            new_buf: Ptr[CChar] := alloc->malloc(alloc, new_cap)
            if new_buf == null {
                if heap_buf != null { alloc->free(alloc, heap_buf) }
                _tl_fatal_("File.read_line: out of memory")
            }
            c_memcpy(new_buf, buf_ptr, len)
            if heap_buf != null { alloc->free(alloc, heap_buf) }
            heap_buf = new_buf
            buf_ptr  = new_buf
            cap      = new_cap
        }

        buf_ptr.[len] = c
        len += 1
        ch = c_fgetc(self->_handle)
    }

    // Check for read error (not just EOF)
    if ch == c_EOF && c_ferror(self->_handle) != 0 {
        if heap_buf != null { alloc->free(alloc, heap_buf) }
        return Err(_errno_to_error(self->_path))
    }

    // Strip trailing \r (Windows line endings)
    if len > 0 && buf_ptr.[len - 1] == '\r' {
        len -= 1
    }

    result := String.from_bytes(alloc, buf_ptr, len)
    if heap_buf != null { alloc->free(alloc, heap_buf) }
    Ok(Some(result))
}

read_line(self) {
    read_line(self, Alloc.context.default)
}

read_all(self, alloc) {
    remaining := try _remaining_bytes(self)
    if remaining == 0 { return Ok(String.empty()) }

    tmp: Ptr[CChar] := alloc->malloc(alloc, remaining + 1)
    if tmp == null { _tl_fatal_("File.read_all: out of memory") }

    bytes_read := c_fread(tmp, 1, remaining, self->_handle)
    if bytes_read < remaining && c_ferror(self->_handle) != 0 {
        alloc->free(alloc, tmp)
        return Err(_errno_to_error(self->_path))
    }

    result := String.from_bytes(alloc, tmp, bytes_read)
    alloc->free(alloc, tmp)
    Ok(result)
}

read_all(self) {
    read_all(self, Alloc.context.default)
}

read_bytes(self, alloc) {
    remaining := try _remaining_bytes(self)
    if remaining == 0 { return Ok(Array.empty[Byte]()) }

    result     := Array.with_capacity_undefined[Byte](alloc, remaining)
    bytes_read := c_fread(result.v, 1, remaining, self->_handle)
    if bytes_read < remaining && c_ferror(self->_handle) != 0 {
        Array.free(result.&, alloc)
        return Err(_errno_to_error(self->_path))
    }
    result.size = bytes_read
    Ok(result)
}

read_bytes(self) {
    read_bytes(self, Alloc.context.default)
}


// -- Streaming write --

write(self, buf, count) {
    n := c_fwrite(buf, 1, count, self->_handle)
    if n < count {
        return Err(_errno_to_error(self->_path))
    }
    Ok(n)
}

write_str(self, data) {
    n := String.len(data)
    if n == 0 { return Ok(void) }
    ptr     := String.cstr(data.&)
    written := c_fwrite(ptr, 1, n, self->_handle)
    if written < n {
        return Err(_errno_to_error(self->_path))
    }
    Ok(void)
}

write_bytes(self, data) {
    if data.size == 0 { return Ok(void) }
    written := c_fwrite(data.v, 1, data.size, self->_handle)
    if written < data.size {
        return Err(_errno_to_error(self->_path))
    }
    Ok(void)
}


// -- Positioning --

seek(self, offset, from) {
    whence := when from {
        s: Start { c_SEEK_SET }
        c: Current { c_SEEK_CUR }
        e: End { c_SEEK_END }
    }
    off: CLong := offset
    if c_fseek(self->_handle, off, whence) != 0 {
        return Err(_errno_to_error(self->_path))
    }
    Ok(void)
}

tell(self) {
    pos := c_ftell(self->_handle)
    if pos < 0 {
        return Err(_errno_to_error(self->_path))
    }
    result: Int := pos
    Ok(result)
}

rewind(self) {
    c_rewind(self->_handle)
}


// -- Query --

path(self) {
    self->_path
}

is_eof(self) {
    c_feof(self->_handle) != 0
}


// -- Convenience --

read_file(alloc, path) {
    f := try open(alloc, path, Read)
    defer close(f)
    read_all(f, alloc)
}

read_file(path) {
    read_file(Alloc.context.default, path)
}

read_file_bytes(alloc, path) {
    f := try open(alloc, path, Read)
    defer close(f)
    read_bytes(f, alloc)
}

read_file_bytes(path) {
    read_file_bytes(Alloc.context.default, path)
}

write_file(path, content) {
    f := try open(path, Write)
    defer close(f)
    write_str(f, content)
}

write_file_bytes(path, data) {
    f := try open(path, Write)
    defer close(f)
    write_bytes(f, data)
}

append_file(path, content) {
    f := try open(path, Append)
    defer close(f)
    write_str(f, content)
}

read_lines(alloc, path) {
    content := try read_file(alloc, path)
    lines   := String.split(alloc, content, _S("\n"))

    // Strip \r from each line (Windows line endings)
    i: CSize := 0
    while i < lines.size, i += 1 {
        line := lines.v.[i]
        n    := String.len(line)
        if n > 0 {
            last: Some := String.char_at(line, n - 1) else { continue }
            if last.v == '\r' {
                lines.v.[i] = String.slice(alloc, line, 0, n - 1)
            }
        }
    }

    // Remove trailing empty line (split on "\n" produces one after final newline)
    if lines.size > 0 && String.is_empty(lines.v.[lines.size - 1]) {
        lines.size -= 1
    }

    Ok(lines)
}

read_lines(path) {
    read_lines(Alloc.context.default, path)
}


// -- File metadata --

exists(path) {
    c_tl_file_exists(String.cstr(path.&)) != 0
}

is_directory(path) {
    c_tl_file_is_dir(String.cstr(path.&)) != 0
}

size(path) {
    sz := c_tl_file_size(String.cstr(path.&))
    if sz < 0 { return Err(_errno_to_error(path)) }
    Ok(sz)
}

temp_dir() {
    buf: CArray[CChar, 4096] := void
    ptr: Ptr[CChar]          := buf
    if c_tl_file_tmpdir(ptr, 4096) != 0 { return _S("/tmp/") }
    String.from_cstr(ptr)
}


// -- Directory operations --

create_dir(path) {
    if c_tl_file_mkdir(String.cstr(path.&)) != 0 {
        return Err(_errno_to_error(path))
    }
    Ok(void)
}

#ifc
_Static_assert(sizeof(tl_path_list) <= 64, "tl_path_list exceeds opaque buffer");

static void tl_scan_dir_start(const char *dir, const char *ext, tl_path_list *out) {
    tl_path_list_init(out);
    tl_scan_dir_impl(dir, ext, out, 0);
}
static size_t tl_scan_dir_count(tl_path_list *l) { return l->count; }
static const char* tl_scan_dir_get(tl_path_list *l, size_t i) { return l->paths[i]; }
static void tl_scan_dir_cleanup(tl_path_list *l) { tl_path_list_free(l); }
#endc

c_struct_tl_path_list: { _opaque: CArray[Byte, 64] }

c_tl_scan_dir_start  (dir: CString, ext: CString, out: Ptr[c_struct_tl_path_list]) -> Void
c_tl_scan_dir_count  (l: Ptr[c_struct_tl_path_list])                               -> CSize
c_tl_scan_dir_get    (l: Ptr[c_struct_tl_path_list], i: CSize)                     -> CString
c_tl_scan_dir_cleanup(l: Ptr[c_struct_tl_path_list])                               -> Void

scan_dir(alloc, path, ext) {
    list: c_struct_tl_path_list := void
    path_cstr                   := String.cstr(path.&)

    ext_cstr: CString := null
    when ext {
        s: Some { ext_cstr = String.cstr(s.v.&) }
        n: None { }
    }

    c_tl_scan_dir_start(path_cstr, ext_cstr, list.&)
    defer c_tl_scan_dir_cleanup(list.&)

    count  := c_tl_scan_dir_count(list.&)
    result := Array.empty[String]()

    i: CSize := 0
    while i < count, i += 1 {
        entry_cstr := c_tl_scan_dir_get(list.&, i)
        Array.push(result.&, alloc, String.from_cstr(alloc, entry_cstr))
    }

    Ok(result)
}

scan_dir(path, ext) {
    scan_dir(Alloc.context.default, path, ext)
}
