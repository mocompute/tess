#ifndef MOS_PLATFORM_H
#define MOS_PLATFORM_H

#ifdef _WIN32
#ifndef MOS_WINDOWS
#define MOS_WINDOWS
#endif
#endif

#ifdef __APPLE__
#define MOS_APPLE
#endif

#ifdef __linux__
#define MOS_LINUX
#endif

#include <stddef.h>

#ifdef MOS_WINDOWS
#include <direct.h>
#include <windows.h>
// windows.h defines macros that conflict with common identifiers
#undef small
#undef OUT
#else
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#endif

// -- High-resolution timing --
#ifdef MOS_WINDOWS
typedef struct {
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    LARGE_INTEGER freq;
} hires_timer;
#else
typedef struct {
    struct timespec start;
    struct timespec end;
} hires_timer;
#endif

void   hires_timer_init(hires_timer *t);
void   hires_timer_start(hires_timer *t);
void   hires_timer_stop(hires_timer *t);
double hires_timer_elapsed_sec(hires_timer *t);

// -- Command existence check --
// Check if a command exists in PATH
// Returns 1 if found, 0 if not
int platform_command_exists(char const *cmd);

// -- Temp file management --
#define PLATFORM_PATH_MAX 4096

typedef struct {
    char path[PLATFORM_PATH_MAX];
} platform_temp_file;

// Create a temp file path with given suffix (e.g., ".c")
// Returns 0 on success, non-zero on failure
int platform_temp_file_create(platform_temp_file *tf, char const *suffix);

// Delete a temp file
void platform_temp_file_delete(platform_temp_file *tf);

// -- Temp directory management --
typedef struct {
    char path[PLATFORM_PATH_MAX];
} platform_temp_path;

// Get platform temp directory path (with trailing separator, e.g., "/tmp/")
// Returns 0 on success, non-zero on failure
int platform_temp_dir(char *buf, size_t bufsize);

// Create a unique temporary directory with given prefix (e.g., "tess-pkg-")
// Returns 0 on success, non-zero on failure
int platform_temp_path_create(platform_temp_path *tp, char const *prefix);

// Remove a directory (must be empty)
void platform_temp_path_delete(char const *path);

// Remove a directory and all its contents recursively
void platform_temp_path_delete_recursive(char const *path);

// -- Directory creation --
// Create a single directory. Returns 0 on success, non-zero on failure.
int platform_mkdir(char const *path);

// -- Process execution --
typedef struct {
    char const *const *argv;       // NULL-terminated argument array
    char const        *stdin_data; // Data to pipe to stdin (can be NULL)
    size_t             stdin_len;  // Length of stdin_data
    int                verbose;    // If non-zero, print command before executing
} platform_exec_opts;

// Execute process, pipe stdin_data to it, forward stderr to parent
// Returns process exit code, or -1 on failure to launch
int platform_exec(platform_exec_opts const *opts);

// Replace current process with the given program.
// On success, does not return. On failure, returns -1.
// argv must be NULL-terminated; argv[0] should be the program path.
int platform_exec_replace(char const *path, char const *const *argv);

#endif
