# Building on Windows with CMake

On Windows, use CMake instead of the Makefile. The CMake build system supports MSVC, MinGW, and Clang.

## Prerequisites
- CMake 3.15 or later
- Visual Studio 2019 or later (for MSVC), or MinGW/Clang
- Git (optional, for version info)

## Initial Configuration
```powershell
# Configure with CMake (from repository root)
cmake -B out/build -S . -G Ninja
```

## Build Configurations
```powershell
# Release build (optimized)
cmake -G Ninja -B out/build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build out/build --config Release

# Debug build (with symbols)
cmake -G Ninja -B out/build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build

# Build with specific number of parallel jobs
cmake --build out/build -j 8
```

## Building the Compiler
```powershell
# Build all targets in parallel
cmake --build out/build -j

# Build specific target
cmake --build out/build --target tess

# Clean and rebuild
cmake --build out/build --target clean
cmake --build out/build
```

## Running Tests
```powershell
# Run all tests using CTest
cd out/build
ctest

# Run tests in parallel
ctest -j 8

# Run tests with verbose output
ctest --verbose

# Run specific test suite
ctest -R test-mos     # MOS library tests
ctest -R test-tess    # Compiler unit tests
ctest -R test-tl      # Tess language integration tests

# Run a specific test by name
ctest -R test_array
ctest -R test_tl_generic
```

## Single Test Execution
To run a single Tess language test on Windows:
```powershell
.\out\build\tess.exe exe src/tess/tl/test_<name>.tl -o test_output.exe
.\test_output.exe
```

## Using the Compiler on Windows
```powershell
# Transpile to C (stdout)
.\out\build\tess.exe c <file.tl>

# Compile to executable
.\out\build\tess.exe exe <file.tl> -o output.exe

# Compile to shared library
.\out\build\tess.exe lib <file.tl> -o output.dll

# Compile to static library
.\out\build\tess.exe lib --static <file.tl> -o output.lib

# Verbose compilation
.\out\build\tess.exe exe -v <file.tl> -o output.exe
```
