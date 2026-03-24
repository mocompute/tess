// Once -- Execute a closure exactly once, even across multiple threads.
//
// Wraps pthread_once on Unix and InitOnceExecuteOnce on Windows.
//
// Usage:
//   #import <Once.tl>
//   once := Once.init()
//   Once.call(once.&, [[alloc]] () { c_printf(c"hello\n") })
#module Once
#import <cstring.tl>


// -- C FFI --

#ifc
#ifdef _WIN32
#include <windows.h>
#define TL_ONCE_SIZE sizeof(INIT_ONCE)

static void tl_once_init(void *buf) {
    InitOnceInitialize((INIT_ONCE *)buf);
}

static BOOL CALLBACK tl_once_trampoline_win(PINIT_ONCE once, PVOID param, PVOID *ctx) {
    (void)once; (void)ctx;
    tl_closure *cl = (tl_closure *)param;
    ((void (*)(void *))cl->fn)(cl->ctx);
    return TRUE;
}

static void tl_once_call(void *buf, tl_closure *closure) {
    InitOnceExecuteOnce((PINIT_ONCE)buf, tl_once_trampoline_win, closure, NULL);
}

#else
#include <pthread.h>
#define TL_ONCE_SIZE sizeof(pthread_once_t)

static void tl_once_init(void *buf) {
    pthread_once_t init_val = PTHREAD_ONCE_INIT;
    memcpy(buf, &init_val, sizeof(pthread_once_t));
}

static _Thread_local tl_closure *tl_once_current_closure;

static void tl_once_trampoline(void) {
    tl_closure *cl = tl_once_current_closure;
    ((void (*)(void *))cl->fn)(cl->ctx);
}

static void tl_once_call(void *buf, tl_closure *closure) {
    tl_once_current_closure = closure;
    pthread_once((pthread_once_t *)buf, tl_once_trampoline);
}

#endif

#endc

c_tl_once_init (buf: Ptr[any])                    -> Void
c_tl_once_call (buf: Ptr[any], closure: Ptr[any]) -> Void


// -- Types --

T: { _buf: CArray[Byte, c_TL_ONCE_SIZE] }


// -- Synopsis --

init ()                             -> T
call (self: Ptr[T], fn: () -> Void) -> Void


// -- Implementation --

init() {
    o: T := void
    c_memset(o.&, 0, sizeof[T]())
    c_tl_once_init(o.&)
    o
}

call(self, fn) {
    c_tl_once_call(self, fn.&)
}
