// Cond -- Condition variable wrapping OS primitives.
//
// Wraps pthread condition variables on Unix and CONDITION_VARIABLE on Windows.
// All fallible operations return Result[T, ThreadError.T].
//
// Usage:
//   #import <Mutex.tl>
//   #import <Cond.tl>
//   cv: Ok := Cond.init() else { return 1 }
//   _w := Cond.wait(cv.v.&, mtx_p)
//   _s := Cond.signal(cv.v.&)
//   Cond.destroy(cv.v.&)
#module Cond
#import <Mutex.tl>
#import <ThreadError.tl>
#import <cstring.tl>


// -- C FFI --

#ifc
#ifdef _WIN32
#include <windows.h>
#define TL_COND_SIZE sizeof(CONDITION_VARIABLE)

static int tl_cond_init(void *buf) {
    InitializeConditionVariable((CONDITION_VARIABLE *)buf);
    return 0;
}

static int tl_cond_wait(void *cond_buf, void *mtx_buf) {
    BOOL ok = SleepConditionVariableCS(
        (CONDITION_VARIABLE *)cond_buf,
        (CRITICAL_SECTION *)mtx_buf,
        INFINITE);
    return ok ? 0 : (int)GetLastError();
}

static int tl_cond_signal(void *buf) {
    WakeConditionVariable((CONDITION_VARIABLE *)buf);
    return 0;
}

static int tl_cond_broadcast(void *buf) {
    WakeAllConditionVariable((CONDITION_VARIABLE *)buf);
    return 0;
}

static void tl_cond_destroy(void *buf) {
    (void)buf;
}

#else
#include <pthread.h>
#define TL_COND_SIZE sizeof(pthread_cond_t)

static int tl_cond_init(void *buf) {
    return pthread_cond_init((pthread_cond_t *)buf, NULL);
}

static int tl_cond_wait(void *cond_buf, void *mtx_buf) {
    return pthread_cond_wait((pthread_cond_t *)cond_buf, (pthread_mutex_t *)mtx_buf);
}

static int tl_cond_signal(void *buf) {
    return pthread_cond_signal((pthread_cond_t *)buf);
}

static int tl_cond_broadcast(void *buf) {
    return pthread_cond_broadcast((pthread_cond_t *)buf);
}

static void tl_cond_destroy(void *buf) {
    pthread_cond_destroy((pthread_cond_t *)buf);
}

#endif

_Static_assert(TL_COND_SIZE <= 48, "TL_COND_SIZE exceeds opaque buffer");
#endc

c_tl_cond_init      (buf: Ptr[any])                        -> CInt
c_tl_cond_wait      (cond_buf: Ptr[any], mtx_buf: Ptr[any]) -> CInt
c_tl_cond_signal    (buf: Ptr[any])                        -> CInt
c_tl_cond_broadcast (buf: Ptr[any])                        -> CInt
c_tl_cond_destroy   (buf: Ptr[any])                        -> Void


// -- Types --

T: {
    _buf: CArray[Byte, 48],
}


// -- Synopsis --

init      ()                          -> Result[T, ThreadError.T]
wait      (self: Ptr[T], mtx: Ptr[Mutex.T]) -> Result[Void, ThreadError.T]
signal    (self: Ptr[T])              -> Result[Void, ThreadError.T]
broadcast (self: Ptr[T])              -> Result[Void, ThreadError.T]
destroy   (self: Ptr[T])              -> Void


// -- Implementation --

init() {
    cv: T := void
    c_memset(cv.&, 0, sizeof[T]())
    rc := c_tl_cond_init(cv.&)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(cv)
}

wait(self, mtx) {
    rc := c_tl_cond_wait(self, mtx)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

signal(self) {
    rc := c_tl_cond_signal(self)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

broadcast(self) {
    rc := c_tl_cond_broadcast(self)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

destroy(self) {
    c_tl_cond_destroy(self)
}
