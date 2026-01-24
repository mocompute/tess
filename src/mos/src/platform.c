#include "platform.h"

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
