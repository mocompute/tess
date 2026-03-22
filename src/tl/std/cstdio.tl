#module stdio
#include <stdio.h>
#include <errno.h>

// Standard streams
c_stdin:  Ptr[c_FILE]
c_stdout: Ptr[c_FILE]
c_stderr: Ptr[c_FILE]

// Constants
c_EOF:       CInt
c_BUFSIZ:    CInt
c_SEEK_SET:  CInt
c_SEEK_CUR:  CInt
c_SEEK_END:  CInt
c_L_tmpnam:  CInt
c_TMP_MAX:   CInt
c_FOPEN_MAX: CInt

// File operations
c_remove  (filename: Ptr[Const[CChar]])                                                -> CInt
c_rename  (old: Ptr[Const[CChar]], new: Ptr[Const[CChar]])                             -> CInt
c_fclose  (stream: Ptr[c_FILE])                                                        -> CInt
c_tmpfile ()                                                                           -> Ptr[c_FILE]
c_tmpnam  (arg0: CString)                                                              -> CString
c_fflush  (stream: Ptr[c_FILE])                                                        -> CInt
c_fopen   (filename: Ptr[Const[CChar]], modes: Ptr[Const[CChar]])                      -> Ptr[c_FILE]
c_freopen (filename: Ptr[Const[CChar]], modes: Ptr[Const[CChar]], stream: Ptr[c_FILE]) -> Ptr[c_FILE]
c_setbuf  (stream: Ptr[c_FILE], buf: CString)                                          -> Void
c_setvbuf (stream: Ptr[c_FILE], buf: CString, modes: CInt, n: CSize)                   -> CInt

// Formatted output
c_fprintf   (stream: Ptr[c_FILE], format: Ptr[Const[CChar]], args: ...)            -> CInt
c_printf    (format: Ptr[Const[CChar]], args: ...)                                 -> CInt
c_sprintf   (s: CString, format: Ptr[Const[CChar]], args: ...)                     -> CInt
c_snprintf  (s: CString, maxlen: CSize, format: Ptr[Const[CChar]], args: ...)      -> CInt
c_vfprintf  (s: Ptr[c_FILE], format: Ptr[Const[CChar]], arg: c_va_list)            -> CInt
c_vprintf   (format: Ptr[Const[CChar]], arg: c_va_list)                            -> CInt
c_vsprintf  (s: CString, format: Ptr[Const[CChar]], arg: c_va_list)                -> CInt
c_vsnprintf (s: CString, maxlen: CSize, format: Ptr[Const[CChar]], arg: c_va_list) -> CInt

// Formatted input
c_fscanf  (stream: Ptr[c_FILE], format: Ptr[Const[CChar]], args: ...)       -> CInt
c_scanf   (format: Ptr[Const[CChar]], args: ...)                            -> CInt
c_sscanf  (s: Ptr[Const[CChar]], format: Ptr[Const[CChar]], args: ...)      -> CInt
c_vfscanf (s: Ptr[c_FILE], format: Ptr[Const[CChar]], arg: c_va_list)       -> CInt
c_vscanf  (format: Ptr[Const[CChar]], arg: c_va_list)                       -> CInt
c_vsscanf (s: Ptr[Const[CChar]], format: Ptr[Const[CChar]], arg: c_va_list) -> CInt

// Character I/O
c_fgetc   (stream: Ptr[c_FILE])          -> CInt
c_getc    (stream: Ptr[c_FILE])          -> CInt
c_getchar ()                             -> CInt
c_fputc   (c: CInt, stream: Ptr[c_FILE]) -> CInt
c_putc    (c: CInt, stream: Ptr[c_FILE]) -> CInt
c_putchar (c: CInt)                      -> CInt

// String I/O
c_fgets  (s: CString, n: CInt, stream: Ptr[c_FILE])  -> CString
c_fputs  (s: Ptr[Const[CChar]], stream: Ptr[c_FILE]) -> CInt
c_puts   (s: Ptr[Const[CChar]])                      -> CInt
c_ungetc (c: CInt, stream: Ptr[c_FILE])              -> CInt

// Block I/O
c_fread  (ptr: Ptr[any], size: CSize, n: CSize, stream: Ptr[c_FILE])   -> CSize
c_fwrite (ptr: Ptr[Const[any]], size: CSize, n: CSize, s: Ptr[c_FILE]) -> CSize

// File positioning
c_fseek   (stream: Ptr[c_FILE], off: CLong, whence: CInt)  -> CInt
c_ftell   (stream: Ptr[c_FILE])                            -> CLong
c_rewind  (stream: Ptr[c_FILE])                            -> Void
c_fgetpos (stream: Ptr[c_FILE], pos: Ptr[c_fpos_t])        -> CInt
c_fsetpos (stream: Ptr[c_FILE], pos: Ptr[Const[c_fpos_t]]) -> CInt

// Error handling
c_clearerr (stream: Ptr[c_FILE])  -> Void
c_feof     (stream: Ptr[c_FILE])  -> CInt
c_ferror   (stream: Ptr[c_FILE])  -> CInt
c_perror   (s: Ptr[Const[CChar]]) -> Void

// errno
c_errno:  CInt
c_ENOENT: CInt
c_EACCES: CInt
c_EEXIST: CInt
