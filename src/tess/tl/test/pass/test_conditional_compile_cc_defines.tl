#module main

// Test that C compiler predefined macros are visible to #ifdef.
// On Linux, __linux__ is defined by the C compiler.
// On macOS, __APPLE__ is defined.
// On Windows, _WIN32 is defined.
// At least one of these must be true on any supported platform.

main() {
    x := 1
    #ifdef __linux__
    x = 0
    #endif
    #ifdef __APPLE__
    x = 0
    #endif
    #ifdef _WIN32
    x = 0
    #endif
    return x
}
