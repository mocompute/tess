// Thread -- Lightweight wrapper around OS threading primitives.
//
// Wraps pthreads on Unix and Win32 threads on Windows. All fallible
// operations return Result[T, ThreadError.T].
//
// Usage:
//   #import <Thread.tl>
//   t: Ok := Thread.spawn([[alloc, capture(ptr)]] () { ptr.* = 42 }) else { return 1 }
//   Thread.join(t.v)
#module Thread
#import <Alloc.tl>
#import <ThreadError.tl>
#import <cstdlib.tl>
#import <cstring.tl>

Allocator = Alloc.Allocator


// -- C FFI --

#ifc
#ifdef _WIN32
#include <windows.h>
#define TL_THREAD_SIZE sizeof(HANDLE)
#else
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <errno.h>
// nanosleep may not be declared if _POSIX_C_SOURCE wasn't set before time.h
// was first included; provide a fallback declaration.
int nanosleep(const struct timespec *req, struct timespec *rem);
#define TL_THREAD_SIZE sizeof(pthread_t)
#endif

_Static_assert(TL_THREAD_SIZE <= 8, "TL_THREAD_SIZE exceeds opaque buffer");

// Forward-declare tl_init so that we can initialize the allocator context
// (and any other TL_THREAD_LOCAL state) in newly-spawned threads.
static void tl_init(void);

// Trampoline for closures: calls fn(ctx), then frees the heap copy.
static void *tl_thread_closure_trampoline(void *arg) {
    tl_init();
    tl_closure *cl = (tl_closure *)arg;
    ((void (*)(void *))cl->fn)(cl->ctx);
    free(cl->ctx);
    free(cl);
    return NULL;
}

// Trampoline for function pointer + argument (spawn/2).
// Tess functions have an implicit first parameter (void *tl_ctx_raw),
// so the actual C signature is: void *fn(void *ctx_raw, void *arg).
typedef struct { void *(*fn)(void *, void *); void *arg; } tl_thread_fn_arg;

static void *tl_thread_fn_trampoline(void *data) {
    tl_init();
    tl_thread_fn_arg *fa = (tl_thread_fn_arg *)data;
    void *(*fn)(void *, void *) = fa->fn;
    void *arg = fa->arg;
    free(fa);
    return fn(NULL, arg);
}

// Spawn a thread from a function pointer + argument.
// fn_closure is a tl_closure*; we extract .fn and ignore .ctx (NULL for named fns).
static int tl_thread_spawn_fn(void *thread_buf, tl_closure *fn_closure, void *arg) {
    tl_thread_fn_arg *fa = (tl_thread_fn_arg *)malloc(sizeof(tl_thread_fn_arg));
    if (!fa) return ENOMEM;
    fa->fn = (void *(*)(void *, void *))fn_closure->fn;
    fa->arg = arg;
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)tl_thread_fn_trampoline, fa, 0, NULL);
    if (!h) { free(fa); return (int)GetLastError(); }
    *(HANDLE *)thread_buf = h;
    return 0;
#else
    int rc = pthread_create((pthread_t *)thread_buf, NULL, tl_thread_fn_trampoline, fa);
    if (rc) free(fa);
    return rc;
#endif
}

// Spawn a thread from a tl_closure pointer (fn+ctx pair).
static int tl_thread_spawn_closure(void *thread_buf, tl_closure *closure_ptr) {
    tl_closure *cl = (tl_closure *)malloc(sizeof(tl_closure));
    if (!cl) return ENOMEM;
    *cl = *closure_ptr;
#ifdef _WIN32
    HANDLE h = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)(void *)tl_thread_closure_trampoline, cl, 0, NULL);
    if (!h) { free(cl); return (int)GetLastError(); }
    *(HANDLE *)thread_buf = h;
    return 0;
#else
    int rc = pthread_create((pthread_t *)thread_buf, NULL, tl_thread_closure_trampoline, cl);
    if (rc) free(cl);
    return rc;
#endif
}

static int tl_thread_join(void *thread_buf, void **retval) {
#ifdef _WIN32
    HANDLE h = *(HANDLE *)thread_buf;
    DWORD r = WaitForSingleObject(h, INFINITE);
    if (r != WAIT_OBJECT_0) return (int)GetLastError();
    CloseHandle(h);
    if (retval) *retval = NULL;
    return 0;
#else
    return pthread_join(*(pthread_t *)thread_buf, retval);
#endif
}

static int tl_thread_detach(void *thread_buf) {
#ifdef _WIN32
    HANDLE h = *(HANDLE *)thread_buf;
    return CloseHandle(h) ? 0 : (int)GetLastError();
#else
    return pthread_detach(*(pthread_t *)thread_buf);
#endif
}

static void tl_thread_yield(void) {
#ifdef _WIN32
    SwitchToThread();
#else
    sched_yield();
#endif
}

static void tl_thread_sleep(unsigned int sec, long nsec) {
#ifdef _WIN32
    DWORD ms = (DWORD)(sec * 1000 + nsec / 1000000);
    Sleep(ms);
#else
    struct timespec ts;
    ts.tv_sec = sec;
    ts.tv_nsec = nsec;
    nanosleep(&ts, NULL);
#endif
}

static void tl_thread_current(void *thread_buf) {
#ifdef _WIN32
    *(HANDLE *)thread_buf = GetCurrentThread();
#else
    *(pthread_t *)thread_buf = pthread_self();
#endif
}

static int tl_thread_equal(void *a, void *b) {
#ifdef _WIN32
    return *(HANDLE *)a == *(HANDLE *)b ? 1 : 0;
#else
    return pthread_equal(*(pthread_t *)a, *(pthread_t *)b) ? 1 : 0;
#endif
}
#endc

c_tl_thread_spawn_fn      (thread_buf: Ptr[any], fn: Ptr[any], arg: Ptr[any]) -> CInt
c_tl_thread_spawn_closure (thread_buf: Ptr[any], closure: Ptr[any])          -> CInt
c_tl_thread_join          (thread_buf: Ptr[any], retval: Ptr[Ptr[any]]) -> CInt
c_tl_thread_detach        (thread_buf: Ptr[any])                        -> CInt
c_tl_thread_yield         ()                                            -> Void
c_tl_thread_sleep         (sec: CUnsignedInt, nsec: CLong)              -> Void
c_tl_thread_current       (thread_buf: Ptr[any])                        -> Void
c_tl_thread_equal         (a: Ptr[any], b: Ptr[any])                    -> CInt


// -- Types --

T: { _buf: CArray[Byte, 8] }


// -- Synopsis --

spawn[A](fn: (Ptr[A]) -> Ptr[any], arg: Ptr[A]) -> Result[T, ThreadError.T]
spawn   (fn: () -> Void)                        -> Result[T, ThreadError.T]
join    (self: T)                        -> Result[Ptr[any], ThreadError.T]
detach  (self: T)                        -> Result[Void, ThreadError.T]
current ()                               -> T
eq      (a: T, b: T)                     -> Bool
yield   ()                               -> Void
sleep   (sec: CUnsignedInt, nsec: CLong) -> Void


// -- Implementation --

spawn[A](fn, arg) {
    t: T := void
    c_memset(t.&, 0, sizeof[T]())
    rc := c_tl_thread_spawn_fn(t.&, fn.&, arg)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(t)
}

spawn(fn) {
    t: T := void
    c_memset(t.&, 0, sizeof[T]())
    rc := c_tl_thread_spawn_closure(t.&, fn.&)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(t)
}

join(self) {
    retval: Ptr[any] := c_NULL
    rc               := c_tl_thread_join(self.&, retval.&)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(retval)
}

detach(self) {
    rc := c_tl_thread_detach(self.&)
    if rc != 0 { return Err(ThreadError.Failed(errno_code = rc)) }
    Ok(void)
}

current() {
    t: T := void
    c_memset(t.&, 0, sizeof[T]())
    c_tl_thread_current(t.&)
    t
}

eq(a, b) {
    c_tl_thread_equal(a.&, b.&) != 0
}

yield() {
    c_tl_thread_yield()
}

sleep(sec, nsec) {
    c_tl_thread_sleep(sec, nsec)
}
