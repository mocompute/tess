#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <process.h>
#else
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
#ifdef MOS_WINDOWS

int platform_command_exists(char const *cmd) {
    char buf[PLATFORM_PATH_MAX];
    snprintf(buf, sizeof(buf), "where %s >nul 2>&1", cmd);
    return system(buf) == 0;
}

#else

int platform_command_exists(char const *cmd) {
    char buf[PLATFORM_PATH_MAX];
    snprintf(buf, sizeof(buf), "command -v %s >/dev/null 2>&1", cmd);
    return system(buf) == 0;
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

int platform_temp_file_create(platform_temp_file *tf, char const *suffix) {
    pid_t pid = getpid();
    snprintf(tf->path, PLATFORM_PATH_MAX, "/tmp/tess_%ld%s", (long)pid, suffix);
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

void platform_temp_path_delete(platform_temp_path *tp) {
    RemoveDirectoryA(tp->path);
}

int platform_mkdir(char const *path) {
    return _mkdir(path);
}

#else

int platform_temp_dir(char *buf, size_t bufsize) {
    snprintf(buf, bufsize, "/tmp/");
    return 0;
}

int platform_temp_path_create(platform_temp_path *tp, char const *prefix) {
    snprintf(tp->path, PLATFORM_PATH_MAX, "/tmp/%sXXXXXX", prefix);
    if (mkdtemp(tp->path) == NULL) return 1;
    return 0;
}

void platform_temp_path_delete(platform_temp_path *tp) {
    rmdir(tp->path);
}

int platform_mkdir(char const *path) {
    return mkdir(path, 0755);
}

#endif

// -- Process execution --
#ifdef MOS_WINDOWS

int platform_exec(platform_exec_opts const *opts) {
    // Build command line from argv
    char   cmdline[8192];
    size_t pos = 0;
    for (int i = 0; opts->argv[i] != NULL; i++) {
        if (i > 0) cmdline[pos++] = ' ';
        // Simple quoting for arguments with spaces
        int    needs_quotes = strchr(opts->argv[i], ' ') != NULL;
        size_t len          = strlen(opts->argv[i]);
        size_t needed       = len + (needs_quotes ? 2 : 0) + 1; // +1 for NUL
        if (pos + needed >= sizeof(cmdline)) {
            fprintf(stderr, "Command line too long (%zu bytes)\n", pos + needed);
            return -1;
        }
        if (needs_quotes) cmdline[pos++] = '"';
        memcpy(cmdline + pos, opts->argv[i], len);
        pos += len;
        if (needs_quotes) cmdline[pos++] = '"';
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
                if (!new_buf) { free(captured); captured = NULL; captured_len = 0; break; }
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

    // Print captured output only on error
    if (!opts->verbose && exit_code != 0 && captured_len > 0) {
        fwrite(captured, 1, captured_len, stderr);
    }
    free(captured);

    return (int)exit_code;
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

    FILE *output_file = fdopen(output_pipe[0], "r");
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
                    if (!new_buf) { free(captured); captured = NULL; captured_len = 0; break; }
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

    // Print captured output only on error
    if (!opts->verbose && exit_code != 0 && captured_len > 0) {
        fwrite(captured, 1, captured_len, stderr);
    }
    free(captured);

    return exit_code;
}

#endif
