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

#ifdef MOS_WINDOWS
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#include <time.h>
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

#endif
