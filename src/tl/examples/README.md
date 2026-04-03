# Tess Examples

Example programs demonstrating various Tess language features. Each example
can be compiled with `tess exe` or run directly with `tess run`.

## Examples

| Example                                           | Description                                                                              |
|---------------------------------------------------|------------------------------------------------------------------------------------------|
| [argc_argv](argc_argv/)                           | Accessing command-line arguments                                                         |
| [wordcount](wordcount/)                           | A full CLI tool using HashMap, File I/O, and CommandLine parsing                         |
| [shared_library](shared_library/)                 | Compiling Tess code into a shared library (`.so`) for C interop                          |
| [static_library](static_library/)                 | Compiling Tess code into a static library (`.a`) for C interop                           |
| [c_export_basic](c_export_basic/)                 | Minimal C interop: a math library built with `tess lib` and called from C via a Makefile |
| [c_export_package_basic](c_export_package_basic/) | Same math library using `package.tl` and a named `MathLib` module                        |
| [c_project_with_tess](c_project_with_tess/)       | Adding a Tess library to an existing C project with a top-level Makefile                 |
| [c_import_basic](c_import_basic/)                 | Importing a C library into Tess using `tess cbind` and `tess c`                          |
| [tess_project_with_c](tess_project_with_c/)       | A Tess project that incorporates a C library from a subdirectory                         |
| [tess_project_with_c_link](tess_project_with_c_link/) | Same as above but using `#link` and `tess exe` instead of `tess c`                   |
| [callbacks](callbacks/)                               | Passing functions, lambdas, and closures to C as callbacks (`fun/N`, `var.&`, `[[alloc, capture()]]`) |
