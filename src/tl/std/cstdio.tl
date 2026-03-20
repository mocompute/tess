#module stdio
#include <stdio.h>
#include <errno.h>

c_printf (fmt: Ptr[Const[CChar]], args: ...)                   -> CInt
c_fprintf(stream: Ptr[any], fmt: Ptr[Const[CChar]], args: ...) -> CInt

// Standard streams.
c_stdin:  Ptr[any]
c_stdout: Ptr[any]
c_stderr: Ptr[any]

// File I/O.
c_fopen  (path: CString, mode: CString)                                 -> Ptr[any]
c_fclose (f: Ptr[any])                                                  -> CInt
c_fread  (buf: Ptr[any], size: CSize, count: CSize, f: Ptr[any])        -> CSize
c_fwrite (buf: Ptr[Const[any]], size: CSize, count: CSize, f: Ptr[any]) -> CSize
c_fseek  (f: Ptr[any], offset: CLong, whence: CInt)                     -> CInt
c_ftell  (f: Ptr[any])                                                  -> CLong
c_rewind (f: Ptr[any])                                                  -> Void
c_feof   (f: Ptr[any])                                                  -> CInt
c_ferror (f: Ptr[any])                                                  -> CInt
c_fgetc  (f: Ptr[any])                                                  -> CInt

// Constants.
c_SEEK_SET: CInt
c_SEEK_CUR: CInt
c_SEEK_END: CInt
c_EOF:      CInt

// errno.
c_errno:  CInt
c_ENOENT: CInt
c_EACCES: CInt
c_EEXIST: CInt
