// Package manifest format version
format(1)

// Package name — determines the output filename and init function name
package(mylib)
author (c"anonymous")
version(c"0.0.1")

// Directory containing .tl source files (searched recursively)
source(c"src/")

// Modules to export from the shared library
export(MyLib)

// Search path for local package dependencies (not needed here)
// depend_path(c"libs/")
