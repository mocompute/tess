#include "platform.h"

#include <ctype.h> // for isalnum
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

// -- High-resolution timing --
#ifdef MOS_WINDOWS

void hires_timer_init(hires_timer *t) {
    QueryPerformanceFrequency(&t->freq);
    QueryPerformanceCounter(&t->start);
    t->end = t->start;
}

void hires_timer_start(hires_timer *t) {
    QueryPerformanceCounter(&t->start);
}

void hires_timer_stop(hires_timer *t) {
    QueryPerformanceCounter(&t->end);
}

double hires_timer_elapsed_sec(hires_timer *t) {
    return (double)(t->end.QuadPart - t->start.QuadPart) / (double)t->freq.QuadPart;
}

#else

void hires_timer_init(hires_timer *t) {
    clock_gettime(CLOCK_MONOTONIC, &t->start);
    t->end = t->start;
}

void hires_timer_start(hires_timer *t) {
    clock_gettime(CLOCK_MONOTONIC, &t->start);
}

void hires_timer_stop(hires_timer *t) {
    clock_gettime(CLOCK_MONOTONIC, &t->end);
}

double hires_timer_elapsed_sec(hires_timer *t) {
    return (double)(t->end.tv_sec - t->start.tv_sec) + (double)(t->end.tv_nsec - t->start.tv_nsec) / 1e9;
}

#endif

// -- Command existence check --

// Reject strings containing path separators or shell metacharacters
static int is_safe_command_name(char const *cmd) {
    for (char const *p = cmd; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ' ' || *p == ';' || *p == '|' || *p == '&' || *p == '\'' ||
            *p == '"' || *p == '`' || *p == '$' || *p == '(' || *p == ')')
            return 0;
    }
    return 1;
}

#ifdef MOS_WINDOWS

int platform_command_exists(char const *cmd) {
    if (!cmd || !*cmd || !is_safe_command_name(cmd)) return 0;
    char buf[PLATFORM_PATH_MAX];
    return SearchPathA(NULL, cmd, ".exe", sizeof(buf), buf, NULL) > 0;
}

#else

int platform_command_exists(char const *cmd) {
    if (!cmd || !*cmd || !is_safe_command_name(cmd)) return 0;
    char const *path_env = getenv("PATH");
    if (!path_env) return 0;
    char const *p = path_env;
    while (*p) {
        char const *end = strchr(p, ':');
        if (!end) end = p + strlen(p);
        size_t dir_len = (size_t)(end - p);
        if (dir_len > 0) {
            char candidate[PLATFORM_PATH_MAX];
            int  cn = snprintf(candidate, sizeof(candidate), "%.*s/%s", (int)dir_len, p, cmd);
            if (cn >= 0 && (size_t)cn < sizeof(candidate)) {
                if (access(candidate, X_OK) == 0) return 1;
            }
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

#endif

// -- Temp file management --
#ifdef MOS_WINDOWS

int platform_temp_file_create(platform_temp_file *tf, char const *suffix) {
    char temp_path[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_path) == 0) {
        return 1;
    }
    DWORD pid = GetCurrentProcessId();
    snprintf(tf->path, PLATFORM_PATH_MAX, "%stess_%lu%s", temp_path, (unsigned long)pid, suffix);
    return 0;
}

void platform_temp_file_delete(platform_temp_file *tf) {
    DeleteFileA(tf->path);
}

#else

static char const *get_temp_dir(void) {
    char const *d = getenv("TMPDIR");
    if (d && d[0]) return d;
    return "/tmp";
}

int platform_temp_file_create(platform_temp_file *tf, char const *suffix) {
    pid_t pid = getpid();
    snprintf(tf->path, PLATFORM_PATH_MAX, "%s/tess_%ld%s", get_temp_dir(), (long)pid, suffix);
    return 0;
}

void platform_temp_file_delete(platform_temp_file *tf) {
    unlink(tf->path);
}

#endif

// -- Temp directory management --
#ifdef MOS_WINDOWS

int platform_temp_dir(char *buf, size_t bufsize) {
    char temp[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp) == 0) return 1;
    snprintf(buf, bufsize, "%s", temp);
    return 0;
}

int platform_temp_path_create(platform_temp_path *tp, char const *prefix) {
    char temp_base[MAX_PATH];
    if (GetTempPathA(MAX_PATH, temp_base) == 0) return 1;
    snprintf(tp->path, PLATFORM_PATH_MAX, "%s%sXXXXXX", temp_base, prefix);
    if (_mktemp(tp->path) == NULL || _mkdir(tp->path) != 0) return 1;
    return 0;
}

void platform_temp_path_delete(char const *path) {
    RemoveDirectoryA(path);
}

#define PLATFORM_RECURSIVE_MAX_DEPTH 64

static void remove_dir_recursive_win(char const *path, int depth) {
    if (depth >= PLATFORM_RECURSIVE_MAX_DEPTH) return;

    char pattern[PLATFORM_PATH_MAX];
    int  n = snprintf(pattern, sizeof(pattern), "%s\\*", path);
    if (n < 0 || (size_t)n >= sizeof(pattern)) return;

    WIN32_FIND_DATAA fd;
    HANDLE           h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == '\0' || (fd.cFileName[1] == '.' && fd.cFileName[2] == '\0')))
            continue;

        char child[PLATFORM_PATH_MAX];
        int  cn = snprintf(child, sizeof(child), "%s\\%s", path, fd.cFileName);
        if (cn < 0 || (size_t)cn >= sizeof(child)) continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            remove_dir_recursive_win(child, depth + 1);
        } else {
            DeleteFileA(child);
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    RemoveDirectoryA(path);
}

void platform_temp_path_delete_recursive(char const *path) {
    if (!path) return;
    remove_dir_recursive_win(path, 0);
}

int platform_mkdir(char const *path) {
    return _mkdir(path);
}

#else

int platform_temp_dir(char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "%s/", get_temp_dir());
    return 0;
}

int platform_temp_path_create(platform_temp_path *tp, char const *prefix) {
    snprintf(tp->path, PLATFORM_PATH_MAX, "%s/%sXXXXXX", get_temp_dir(), prefix);
    if (mkdtemp(tp->path) == NULL) return 1;
    return 0;
}

void platform_temp_path_delete(char const *path) {
    rmdir(path);
}

#define PLATFORM_RECURSIVE_MAX_DEPTH 64

static void remove_dir_recursive(char const *path, int depth) {
    if (depth >= PLATFORM_RECURSIVE_MAX_DEPTH) return;

    DIR *d = opendir(path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.' &&
            (ent->d_name[1] == '\0' || (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
            continue;

        char child[PLATFORM_PATH_MAX];
        int  cn = snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
        if (cn < 0 || (size_t)cn >= sizeof(child)) continue;

        struct stat st;
        if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) {
            remove_dir_recursive(child, depth + 1);
        } else {
            unlink(child);
        }
    }

    closedir(d);
    rmdir(path);
}

void platform_temp_path_delete_recursive(char const *path) {
    if (!path) return;
    remove_dir_recursive(path, 0);
}

int platform_mkdir(char const *path) {
    return mkdir(path, 0755);
}

#endif

// -- Process execution --
#ifdef MOS_WINDOWS

// Append a quoted argument to buf using CommandLineToArgvW-compatible rules:
// - Always wrap in double quotes
// - N backslashes before a " or end-of-arg become 2N (+ \" if before quote)
// - N backslashes elsewhere stay literal
// - Bare " becomes \"
// Returns new position, or -1 on overflow.
static int append_quoted_arg(char *buf, size_t bufsize, size_t pos, char const *arg) {
    if (pos >= bufsize) return -1;
    buf[pos++] = '"';

    for (char const *p = arg; *p;) {
        size_t num_backslashes = 0;
        while (p[num_backslashes] == '\\') num_backslashes++;

        if (p[num_backslashes] == '\0') {
            // End of arg: double the backslashes
            size_t need = num_backslashes * 2;
            if (pos + need >= bufsize) return -1;
            for (size_t i = 0; i < need; i++) buf[pos++] = '\\';
            p += num_backslashes;
        } else if (p[num_backslashes] == '"') {
            // Quote: double the backslashes + escape the quote
            size_t need = num_backslashes * 2 + 2; // 2N backslashes + \"
            if (pos + need >= bufsize) return -1;
            for (size_t i = 0; i < num_backslashes * 2; i++) buf[pos++] = '\\';
            buf[pos++] = '\\';
            buf[pos++] = '"';
            p += num_backslashes + 1;
        } else {
            // Not followed by quote: backslashes are literal
            size_t need = num_backslashes + 1;
            if (pos + need >= bufsize) return -1;
            for (size_t i = 0; i < num_backslashes; i++) buf[pos++] = '\\';
            buf[pos++] = p[num_backslashes];
            p += num_backslashes + 1;
        }
    }

    if (pos + 1 >= bufsize) return -1;
    buf[pos++] = '"';
    return (int)pos;
}

int platform_exec(platform_exec_opts const *opts) {
    // Build command line from argv using proper quoting
    char   cmdline[8192];
    size_t pos = 0;
    for (int i = 0; opts->argv[i] != NULL; i++) {
        if (i > 0) {
            if (pos >= sizeof(cmdline)) {
                fprintf(stderr, "Command line too long\n");
                return -1;
            }
            cmdline[pos++] = ' ';
        }
        int result = append_quoted_arg(cmdline, sizeof(cmdline), pos, opts->argv[i]);
        if (result < 0) {
            fprintf(stderr, "Command line too long\n");
            return -1;
        }
        pos = (size_t)result;
    }
    if (pos >= sizeof(cmdline)) {
        fprintf(stderr, "Command line too long\n");
        return -1;
    }
    cmdline[pos] = '\0';

    if (opts->verbose) {
        fprintf(stderr, "Running: %s\n", cmdline);
    }

    // Create a single pipe for both stdout and stderr.
    // Merging them avoids deadlocks from sequential reads and ensures
    // we capture MSVC output (which goes to stdout, not stderr).
    HANDLE              output_read, output_write;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    if (!CreatePipe(&output_read, &output_write, &sa, 0)) {
        fprintf(stderr, "CreatePipe failed\n");
        return -1;
    }
    SetHandleInformation(output_read, HANDLE_FLAG_INHERIT, 0);

    // Set up process startup info
    STARTUPINFOA si             = {sizeof(STARTUPINFOA)};
    si.dwFlags                  = STARTF_USESTDHANDLES;
    si.hStdInput                = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput               = output_write;
    si.hStdError                = output_write;

    PROCESS_INFORMATION pi      = {0};

    BOOL                success = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    CloseHandle(output_write);

    if (!success) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        CloseHandle(output_read);
        return -1;
    }

    // Read all output (merged stdout+stderr)
    char   buf[1024];
    DWORD  bytes_read;
    char  *captured     = NULL;
    size_t captured_len = 0;
    size_t captured_cap = 0;

    while (ReadFile(output_read, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
        if (opts->verbose) {
            // In verbose mode, forward output in real-time
            buf[bytes_read] = '\0';
            fprintf(stderr, "%s", buf);
        } else {
            // Buffer output for deferred display
            if (captured_len + bytes_read > captured_cap) {
                captured_cap = (captured_len + bytes_read) * 2;
                if (captured_cap < 4096) captured_cap = 4096;
                char *new_buf = realloc(captured, captured_cap);
                if (!new_buf) {
                    free(captured);
                    captured     = NULL;
                    captured_len = 0;
                    break;
                }
                captured = new_buf;
            }
            memcpy(captured + captured_len, buf, bytes_read);
            captured_len += bytes_read;
        }
    }
    CloseHandle(output_read);

    // Wait for process and get exit code
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    // Return or free captured output
    if (opts->captured_output) {
        *opts->captured_output = captured;
        if (opts->captured_output_len) *opts->captured_output_len = captured_len;
    } else {
        if (!opts->verbose && exit_code != 0 && captured_len > 0) {
            fwrite(captured, 1, captured_len, stderr);
        }
        free(captured);
    }

    return (int)exit_code;
}

int platform_exec_replace(char const *path, char const *const *argv) {
    _execv(path, argv);
    return -1;
}

#else

int platform_exec(platform_exec_opts const *opts) {
    if (!opts->argv[0]) return -1;

    if (opts->verbose) {
        fprintf(stderr, "Running:");
        for (int i = 0; opts->argv[i] != NULL; i++) {
            fprintf(stderr, " %s", opts->argv[i]);
        }
        fprintf(stderr, "\n");
    }

    // Use a single output pipe for both stdout and stderr to avoid deadlocks
    int stdin_pipe[2], output_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(output_pipe) == -1) {
        perror("pipe failed");
        return -1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }

    if (pid == 0) {
        // child process
        close(stdin_pipe[1]);
        close(output_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(output_pipe[1]);

        execvp(opts->argv[0], (char *const *)opts->argv);
        perror("exec failed");
        _exit(127);
    }

    // parent process
    close(stdin_pipe[0]);
    close(output_pipe[1]);

    // Write stdin data if provided
    if (opts->stdin_data && opts->stdin_len > 0) {
        ssize_t ignore = write(stdin_pipe[1], opts->stdin_data, opts->stdin_len);
        (void)ignore;
    }
    close(stdin_pipe[1]);

    // Read merged stdout+stderr
    char   buf[1024];
    char  *captured     = NULL;
    size_t captured_len = 0;
    size_t captured_cap = 0;

    FILE  *output_file  = fdopen(output_pipe[0], "r");
    if (output_file) {
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), output_file)) > 0) {
            if (opts->verbose) {
                fwrite(buf, 1, n, stderr);
            } else {
                if (captured_len + n > captured_cap) {
                    captured_cap = (captured_len + n) * 2;
                    if (captured_cap < 4096) captured_cap = 4096;
                    char *new_buf = realloc(captured, captured_cap);
                    if (!new_buf) {
                        free(captured);
                        captured     = NULL;
                        captured_len = 0;
                        break;
                    }
                    captured = new_buf;
                }
                memcpy(captured + captured_len, buf, n);
                captured_len += n;
            }
        }
        fclose(output_file);
    }

    // Wait for child and get exit status
    int status;
    waitpid(pid, &status, 0);

    int exit_code = -1;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }

    // Return or free captured output
    if (opts->captured_output) {
        *opts->captured_output = captured;
        if (opts->captured_output_len) *opts->captured_output_len = captured_len;
    } else {
        if (!opts->verbose && exit_code != 0 && captured_len > 0) {
            fwrite(captured, 1, captured_len, stderr);
        }
        free(captured);
    }

    return exit_code;
}

int platform_exec_replace(char const *path, char const *const *argv) {
    execv(path, (char *const *)argv);
    return -1;
}

#endif

// -- String utilities

size_t platform_make_c_identifier(char *dest, char const *src, size_t len) {
    size_t j = 0;
    for (size_t i = 0; src[i] && j < len - 1; i++) {
        if (isalnum((unsigned char)src[i])) {
            dest[j++] = src[i];
        } else {
            dest[j++] = '_';
        }
    }
    dest[j] = '\0';
    return j;
}
