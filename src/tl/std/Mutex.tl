// Mutex -- Mutual exclusion lock wrapping OS primitives.
//
// Wraps pthreads mutexes on Unix and CRITICAL_SECTION on Windows.
// All fallible operations return Result[T, ThreadError.T].
//
// Usage:
//   #import <Mutex.tl>
//   mtx: Ok := Mutex.init() else { return 1 }
//   _l := Mutex.lock(mtx.v.&)
//   // ... critical section ...
//   _u := Mutex.unlock(mtx.v.&)
//   Mutex.destroy(mtx.v.&)
#module Mutex
#import <ThreadError.tl>
#import <cstring.tl>


// -- C FFI --

#ifc
#ifdef _WIN32
#include <windows.h>
#define TL_MUTEX_SIZE sizeof(CRITICAL_SECTION)

static int tl_mutex_init(void *buf, int kind) {
    (void)kind;
    InitializeCriticalSection((CRITICAL_SECTION *)buf);
    return 0;
}

static int tl_mutex_lock(void *buf) {
    EnterCriticalSection((CRITICAL_SECTION *)buf);
    return 0;
}

static int tl_mutex_unlock(void *buf) {
    LeaveCriticalSection((CRITICAL_SECTION *)buf);
    return 0;
}

static int tl_mutex_trylock(void *buf) {
    return TryEnterCriticalSection((CRITICAL_SECTION *)buf) ? 1 : 0;
}

static void tl_mutex_destroy(void *buf) {
    DeleteCriticalSection((CRITICAL_SECTION *)buf);
}

#else
#include <pthread.h>
#include <errno.h>
#define TL_MUTEX_SIZE sizeof(pthread_mutex_t)

// Use _NP variants on Linux; standard names on macOS/other POSIX.
#ifndef PTHREAD_MUTEX_DEFAULT
#ifdef PTHREAD_MUTEX_DEFAULT_NP
#define PTHREAD_MUTEX_DEFAULT    PTHREAD_MUTEX_DEFAULT_NP
#else
#define PTHREAD_MUTEX_DEFAULT    0
#endif
#endif
#ifndef PTHREAD_MUTEX_RECURSIVE
#ifdef PTHREAD_MUTEX_RECURSIVE_NP
#define PTHREAD_MUTEX_RECURSIVE  PTHREAD_MUTEX_RECURSIVE_NP
#else
#define PTHREAD_MUTEX_RECURSIVE  1
#endif
#endif
#ifndef PTHREAD_MUTEX_ERRORCHECK
#ifdef PTHREAD_MUTEX_ERRORCHECK_NP
#define PTHREAD_MUTEX_ERRORCHECK PTHREAD_MUTEX_ERRORCHECK_NP
#else
#define PTHREAD_MUTEX_ERRORCHECK 2
#endif
#endif

// pthread_mutexattr_settype may not be declared without feature macros.
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);

static int tl_mutex_init(void *buf, int kind) {
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    int type = PTHREAD_MUTEX_DEFAULT;
    if (kind == 1) type = PTHREAD_MUTEX_RECURSIVE;
    else if (kind == 2) type = PTHREAD_MUTEX_ERRORCHECK;
    pthread_mutexattr_settype(&attr, type);
    int rc = pthread_mutex_init((pthread_mutex_t *)buf, &attr);
    pthread_mutexattr_destroy(&attr);
    return rc;
}

static int tl_mutex_lock(void *buf) {
    return pthread_mutex_lock((pthread_mutex_t *)buf);
}

static int tl_mutex_unlock(void *buf) {
    return pthread_mutex_unlock((pthread_mutex_t *)buf);
}

static int tl_mutex_trylock(void *buf) {
    int rc = pthread_mutex_trylock((pthread_mutex_t *)buf);
    if (rc == 0) return 1;
    if (rc == EBUSY) return 0;
    return -rc;
}

static void tl_mutex_destroy(void *buf) {
    pthread_mutex_destroy((pthread_mutex_t *)buf);
}

#endif

_Static_assert(TL_MUTEX_SIZE <= 64, "TL_MUTEX_SIZE exceeds opaque buffer");
#endc

c_tl_mutex_init    (buf: Ptr[any], kind: CInt) -> CInt
c_tl_mutex_lock    (buf: Ptr[any])             -> CInt
c_tl_mutex_unlock  (buf: Ptr[any])             -> CInt
c_tl_mutex_trylock (buf: Ptr[any])             -> CInt
c_tl_mutex_destroy (buf: Ptr[any])             -> Void


// -- Types --

Kind: | Plain
      | Recursive
      | ErrorCheck

T: {
    _buf:  CArray[Byte, 64],
    _kind: Kind,
}


// -- Synopsis --

init     (kind: Kind)      -> Result[T, ThreadError.T]
init     ()                -> Result[T, ThreadError.T]
lock     (self: Ptr[T])    -> Result[Void, ThreadError.T]
unlock   (self: Ptr[T])    -> Result[Void, ThreadError.T]
try_lock (self: Ptr[T])    -> Result[Bool, ThreadError.T]
destroy  (self: Ptr[T])    -> Void


// -- Implementation --

init(kind) {
    mtx: T := void
    c_memset(mtx.&, 0, sizeof[T]())

    k: CInt := when kind {
        _p: Plain      { 0 }
        _r: Recursive  { 1 }
        _e: ErrorCheck { 2 }
    }

    rc := c_tl_mutex_init(mtx.&, k)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    mtx._kind = kind
    Ok(mtx)
}

init() {
    init(Plain)
}

lock(self) {
    rc := c_tl_mutex_lock(self)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

unlock(self) {
    rc := c_tl_mutex_unlock(self)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

try_lock(self) {
    rc := c_tl_mutex_trylock(self)
    if rc < 0 { return Err(ThreadError.Failed(errno_code = -rc)) }
    Ok(rc == 1)
}

destroy(self) {
    c_tl_mutex_destroy(self)
}
