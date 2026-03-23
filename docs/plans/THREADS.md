# Threads Library Design

A threading library for Tess based on POSIX pthreads (Linux/macOS) with a Win32 threads backend (Windows).

## Goals

- Provide thread creation, mutexes, condition variables, and one-time initialization
- Type-safe Tess API with `Result`-based error handling
- Platform-correct opaque types using C macros for sizes (the `c_` prefix pattern from `Alloc.tl`)
- Thin wrappers over platform APIs — minimal C helper code via `#ifc`/`#endc`

## Non-Goals

- Thread-local storage API (Tess globals are already thread-local)
- Timed waits on condition variables (deferred — absolute vs relative time conversion adds complexity)
- `Mutex[T]` data-guarding pattern (standalone locks only, matching C model)

## Modules

Five new files in `src/tl/std/`, all explicitly imported (`#import <Thread.tl>`, etc.):

| File | Module | Purpose |
|------|--------|---------|
| `ThreadError.tl` | `ThreadError` | Shared error type |
| `Thread.tl` | `Thread` | Thread creation, join, detach, sleep, yield |
| `Mutex.tl` | `Mutex` | Plain, recursive, error-checking mutexes |
| `Cond.tl` | `Cond` | Condition variables |
| `Once.tl` | `Once` | One-time initialization |

## Error Type (`ThreadError.tl`)

```tl
#module ThreadError

T: | Failed { errno_code: CInt }
```

All fallible operations across all four modules return `Result[T, ThreadError]` or `Result[Void, ThreadError]`. The `errno_code` carries the raw platform error code for callers who need to inspect it.

## Platform Abstraction Pattern

Each module uses the same structure:

1. **`#ifc`/`#endc` block** at the top: includes platform headers, defines a C macro for the opaque buffer size (platform-branched via `sizeof`), and defines C helper functions with platform-branched implementations.

2. **`c_` macro access** for the size constant: the Tess type uses `c_TL_MUTEX_SIZE` (etc.) to get the platform-correct size. This follows the same pattern as `Alloc.tl`'s `c_TL_ALLOC_TRANSIENT_SIZE`.

3. **`c_` FFI declarations** below the `#ifc` block: thin bindings to the C helpers.

4. **Tess API functions**: call `c_` functions and translate integer returns to `Result`.

Example pattern:

```tl
#ifc
#if defined(_WIN32)
  #include <windows.h>
  #define TL_MUTEX_SIZE sizeof(CRITICAL_SECTION)
#elif defined(__APPLE__)
  #include <pthread.h>
  #define TL_MUTEX_SIZE sizeof(pthread_mutex_t)
#elif defined(__linux__)
  #include <pthread.h>
  #define TL_MUTEX_SIZE sizeof(pthread_mutex_t)
#else
  #error "Unsupported platform"
#endif

static int tl_mutex_init(void *buf, int kind) {
    // platform-branched implementation
}
#endc

c_TL_MUTEX_SIZE: CSize
c_tl_mutex_init(buf: Ptr[any], kind: CInt) -> CInt

T: { _handle: CArray[Byte, c_TL_MUTEX_SIZE] }

init(kind: Kind) -> Result[Mutex, ThreadError] {
    m: Mutex := void
    c_memset(m.&, 0, sizeof[Mutex]())
    rc := c_tl_mutex_init(m._handle.&, kind_to_int(kind))
    if rc != 0 {
        return Err(ThreadError.Failed(errno_code = rc))
    }
    Ok(m)
}
```

## Thread Module (`Thread.tl`)

### Opaque Type

The `#ifc` block defines `TL_THREAD_SIZE` using `sizeof(HANDLE)` / `sizeof(pthread_t)` per platform.

```tl
T: { _handle: CArray[Byte, c_TL_THREAD_SIZE] }
```

### API

```tl
// Spawn with typed function pointer + argument
spawn[T](fn: (Ptr[T]) -> Ptr[any], arg: Ptr[T]) -> Result[Thread, ThreadError]

// Spawn with allocated closure (must use [[alloc, capture(...)]])
// Captured values are copied; use Ptr captures for shared mutable state.
// The thread takes ownership of the closure and frees it after invocation.
spawn(fn: () -> Void) -> Result[Thread, ThreadError]

// Wait for thread to finish, get its return value
join(self: Thread) -> Result[Ptr[any], ThreadError]

// Detach thread (runs independently, no join needed)
detach(self: Thread) -> Result[Void, ThreadError]

// Current thread utilities
yield() -> Void
sleep(seconds: CUnsignedInt, nanoseconds: CLong) -> Void

// Identity
current() -> Thread
eq(a: Thread, b: Thread) -> Bool
```

### Implementation Notes

- **`spawn/2` (generic)**: A C trampoline in the `#ifc` block casts `void *` back to the correct pointer type. Monomorphization gives each specialization its own trampoline with the right cast.
- **`spawn/1` (closure)**: Takes an **allocated** closure (`[[alloc, capture(...)]]`). The `tl_closure` struct (fn + ctx pointers) is copied to a heap allocation and passed as the platform's `void *` / `LPVOID` argument. A C trampoline invokes the closure, then frees both the ctx and the `tl_closure` wrapper. Shared mutable state must be captured as `Ptr` (e.g., `counter_p := counter.&`, then `capture(counter_p)`). On Windows, the trampoline signature matches `LPTHREAD_START_ROUTINE` (`DWORD WINAPI fn(LPVOID)`); on pthreads it matches `void *(*)(void *)`.
- **`join`** returns `Ptr[any]` — caller casts. On pthreads: `pthread_join`. On Windows: `WaitForSingleObject` + `GetExitCodeThread`.
- **`sleep`** wraps `nanosleep` on Unix, `Sleep` (millisecond granularity) on Windows.
- **`eq`** wraps `pthread_equal` on Unix. On Windows, compares thread IDs.

### Platform Backends

| Operation | pthreads (Linux/macOS) | Win32 (Windows) |
|-----------|----------------------|-----------------|
| spawn | `pthread_create` | `CreateThread` |
| join | `pthread_join` | `WaitForSingleObject` + `CloseHandle` |
| detach | `pthread_detach` | `CloseHandle` |
| yield | `sched_yield` | `SwitchToThread` |
| sleep | `nanosleep` | `Sleep` |
| current | `pthread_self` | `GetCurrentThread` |
| eq | `pthread_equal` | Thread ID comparison |

## Mutex Module (`Mutex.tl`)

### Kind

```tl
Kind: | Plain
      | Recursive
      | ErrorCheck
```

### Opaque Type

The `#ifc` block defines `TL_MUTEX_SIZE` per platform (see Platform Abstraction Pattern above).

```tl
T: { _handle: CArray[Byte, c_TL_MUTEX_SIZE]
     _kind: Kind }
```

On Windows, `CRITICAL_SECTION` is natively recursive — the `_kind` field is used by the C helpers to emulate plain and error-checking semantics via manual lock ownership tracking. On pthreads, `_kind` is redundant (the mutex type is set via attributes) but kept for a uniform struct layout.

### API

```tl
// Initialize with a specific kind
init(kind: Kind) -> Result[Mutex, ThreadError]

// Convenience: plain mutex
init() -> Result[Mutex, ThreadError]

// Lock / unlock
lock(self: Ptr[Mutex]) -> Result[Void, ThreadError]
unlock(self: Ptr[Mutex]) -> Result[Void, ThreadError]

// Non-blocking lock attempt: true = acquired, false = already held
try_lock(self: Ptr[Mutex]) -> Result[Bool, ThreadError]

// Cleanup
destroy(self: Ptr[Mutex]) -> Void
```

### Implementation Notes

- `init/0` calls `init(Kind.Plain)`.
- On **pthreads**: mutex kind is set via `pthread_mutexattr_settype` (`PTHREAD_MUTEX_DEFAULT`, `PTHREAD_MUTEX_RECURSIVE`, `PTHREAD_MUTEX_ERRORCHECK`). Maps to `pthread_mutex_init`, `pthread_mutex_lock`, `pthread_mutex_unlock`, `pthread_mutex_trylock`, `pthread_mutex_destroy`.
- On **Windows**: `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`, `TryEnterCriticalSection`, `DeleteCriticalSection`. For `Plain` and `ErrorCheck` kinds, a wrapper tracks lock ownership to detect double-lock or wrong-thread-unlock.
- `destroy` returns `Void` — errors from `pthread_mutex_destroy` only occur on misuse (destroying a locked mutex).

## Cond Module (`Cond.tl`)

### Opaque Type

The `#ifc` block defines `TL_COND_SIZE` using `sizeof(CONDITION_VARIABLE)` / `sizeof(pthread_cond_t)` per platform.

```tl
T: { _handle: CArray[Byte, c_TL_COND_SIZE] }
```

### API

```tl
// Initialize
init() -> Result[Cond, ThreadError]

// Wait (releases mutex, blocks until signaled, re-acquires mutex)
wait(self: Ptr[Cond], mtx: Ptr[Mutex]) -> Result[Void, ThreadError]

// Wake one waiting thread
signal(self: Ptr[Cond]) -> Result[Void, ThreadError]

// Wake all waiting threads
broadcast(self: Ptr[Cond]) -> Result[Void, ThreadError]

// Cleanup
destroy(self: Ptr[Cond]) -> Void
```

### Implementation Notes

- On **pthreads**: `pthread_cond_init`, `pthread_cond_wait`, `pthread_cond_signal`, `pthread_cond_broadcast`, `pthread_cond_destroy`.
- On **Windows**: `InitializeConditionVariable`, `SleepConditionVariableCS`, `WakeConditionVariable`, `WakeAllConditionVariable`. No destroy needed (Windows condvars are statically sized), but `destroy` is a no-op for API consistency.
- Timed wait is deferred — `pthread_cond_timedwait` uses absolute `struct timespec` while Windows uses relative milliseconds. The conversion adds complexity for a later iteration.

## Once Module (`Once.tl`)

### Opaque Type

The `#ifc` block defines `TL_ONCE_SIZE` using `sizeof(INIT_ONCE)` / `sizeof(pthread_once_t)` per platform.

```tl
T: { _handle: CArray[Byte, c_TL_ONCE_SIZE] }
```

### API

```tl
// Initialize (not fallible — zero-initializes the buffer)
init() -> Once

// Execute fn exactly once, regardless of how many threads call it
call(self: Ptr[Once], fn: () -> Void) -> Void
```

### Implementation Notes

- `init` zero-initializes the buffer (equivalent to `PTHREAD_ONCE_INIT` / `INIT_ONCE_STATIC_INIT`).
- On **pthreads**: `pthread_once`. The closure is passed through a `_Thread_local` variable since `pthread_once` doesn't accept a user-data argument — the C trampoline reads the closure from TLS.
- On **Windows**: `InitOnceExecuteOnce` with `PINIT_ONCE_FN` callback. The closure pointer is passed through the `Parameter` argument.
- `call` is not fallible — `pthread_once` cannot meaningfully fail in practice.

## Linking Strategy

The compiler must pass platform-specific linker flags when threading modules are in use.

### Required Flags

| Platform | Flag | Notes |
|----------|------|-------|
| Linux | `-lpthread` | Required for all pthreads functions |
| macOS | `-lpthread` | Often a no-op (pthreads is in libSystem), still needed for portability |
| Windows | none | Win32 thread functions are in `kernel32.lib`, linked by default |

### Detection Mechanism

**Implicit detection via import graph.** The compiler already tracks which files are imported via `source_scanner.c` and `import_resolver.c`. During the link phase in `tess_exe.c`, if any of the threading modules (`ThreadError.tl`, `Thread.tl`, `Mutex.tl`, `Cond.tl`, `Once.tl`) appears in the import graph, `-lpthread` is added to the linker invocation on non-Windows platforms.

Since `ThreadError.tl` is imported by all four other modules, checking for `ThreadError.tl` alone is sufficient as a sentinel.

Implementation in `tess_exe.c`:

```c
#ifndef _WIN32
if (imports_any_of(ctx, "ThreadError.tl")) {
    add_linker_flag("-lpthread");
}
#endif
```

## Testing

Four test files in `src/tess/tl/test/pass/`, auto-discovered:

| File | Tests |
|------|-------|
| `test_thread.tl` | spawn/2 with function pointer, spawn/1 with closure, join, detach, yield |
| `test_mutex.tl` | All three kinds (plain, recursive, error-checking), lock/unlock, try_lock, contention between threads |
| `test_cond.tl` | wait/signal/broadcast with a producer-consumer pattern |
| `test_once.tl` | Multiple threads calling `Once.call`, verify function runs exactly once |

Tests use the `T()` macro pattern, return `CInt` (0 = pass). All tests spawn real threads for actual concurrent behavior.
