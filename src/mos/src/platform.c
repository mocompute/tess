#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef MOS_WINDOWS
#include <fcntl.h>
#include <io.h>
#include <process.h>
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

// -- Process execution --
#ifdef MOS_WINDOWS

int platform_exec(platform_exec_opts const *opts) {
    // Build command line from argv
    char cmdline[8192];
    size_t pos = 0;
    for (int i = 0; opts->argv[i] != NULL; i++) {
        if (i > 0) cmdline[pos++] = ' ';
        // Simple quoting for arguments with spaces
        int needs_quotes = strchr(opts->argv[i], ' ') != NULL;
        if (needs_quotes) cmdline[pos++] = '"';
        size_t len = strlen(opts->argv[i]);
        memcpy(cmdline + pos, opts->argv[i], len);
        pos += len;
        if (needs_quotes) cmdline[pos++] = '"';
    }
    cmdline[pos] = '\0';

    if (opts->verbose) {
        fprintf(stderr, "Running: %s\n", cmdline);
    }

    // Create pipes for stdout and stderr
    HANDLE stdout_read, stdout_write;
    HANDLE stderr_read, stderr_write;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    if (!CreatePipe(&stdout_read, &stdout_write, &sa, 0)) {
        fprintf(stderr, "CreatePipe failed\n");
        return -1;
    }
    SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&stderr_read, &stderr_write, &sa, 0)) {
        fprintf(stderr, "CreatePipe failed\n");
        CloseHandle(stdout_read);
        CloseHandle(stdout_write);
        return -1;
    }
    SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0);

    // Set up process startup info
    STARTUPINFOA si = {sizeof(STARTUPINFOA)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stdout_write;
    si.hStdError = stderr_write;

    PROCESS_INFORMATION pi = {0};

    BOOL success = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);

    CloseHandle(stdout_write);
    CloseHandle(stderr_write);

    if (!success) {
        fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
        CloseHandle(stdout_read);
        CloseHandle(stderr_read);
        return -1;
    }

    // Drain stdout (discard) and read stderr
    char buf[1024];
    DWORD bytes_read;
    while (ReadFile(stdout_read, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
        // Discard stdout output
    }
    CloseHandle(stdout_read);
    while (ReadFile(stderr_read, buf, sizeof(buf) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buf[bytes_read] = '\0';
        fprintf(stderr, "%s", buf);
    }
    CloseHandle(stderr_read);

    // Wait for process and get exit code
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return (int)exit_code;
}

#else

int platform_exec(platform_exec_opts const *opts) {
    if (opts->verbose) {
        fprintf(stderr, "Running:");
        for (int i = 0; opts->argv[i] != NULL; i++) {
            fprintf(stderr, " %s", opts->argv[i]);
        }
        fprintf(stderr, "\n");
    }

    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) == -1 || pipe(stdout_pipe) == -1 || pipe(stderr_pipe) == -1) {
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
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        execvp(opts->argv[0], (char *const *)opts->argv);
        perror("exec failed");
        _exit(127);
    }

    // parent process
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    // Write stdin data if provided
    if (opts->stdin_data && opts->stdin_len > 0) {
        ssize_t ignore = write(stdin_pipe[1], opts->stdin_data, opts->stdin_len);
        (void)ignore;
    }
    close(stdin_pipe[1]);

    // Read and forward stderr
    char buf[1024];
    FILE *stderr_file = fdopen(stderr_pipe[0], "r");
    while (fgets(buf, sizeof(buf), stderr_file)) {
        fprintf(stderr, "%s", buf);
    }
    fclose(stderr_file);

    // Wait for child and get exit status
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
}

#endif
