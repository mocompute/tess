# Threads Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement Thread, Mutex, Cond, and Once modules for Tess, wrapping pthreads on Linux/macOS and Win32 threads on Windows.

**Architecture:** Five new `.tl` files in `src/tl/std/` (ThreadError, Thread, Mutex, Cond, Once). Each uses `#ifc`/`#endc` for platform-branched C helpers, `c_` prefix macros for opaque buffer sizes, and `Result`-based error handling. The compiler gains automatic `-lpthread` linking when threading modules are imported.

**Tech Stack:** C11, pthreads (Linux/macOS), Win32 threads (Windows), Tess standard library conventions

**Spec:** `docs/plans/THREADS.md`

---

### Task 1: ThreadError module

**Files:**
- Create: `src/tl/std/ThreadError.tl`

This is the shared error type imported by all other threading modules.

- [ ] **Step 1: Create `ThreadError.tl`**

```tl
#module ThreadError

T: | Failed { errno_code: CInt }
```

- [ ] **Step 2: Verify it compiles**

Run: `./tess c src/tl/std/ThreadError.tl`
Expected: Transpiles to C without errors.

- [ ] **Step 3: Commit**

```bash
git add src/tl/std/ThreadError.tl
git commit -m "add ThreadError module: shared error type for threading"
```

---

### Task 2: Linker flag support (`-lpthread`)

**Files:**
- Modify: `src/tess/src/tess_exe.c`

The compiler must automatically pass `-lpthread` on non-Windows platforms when any threading module is imported. Detection is via the `modules_seen` hashmap in the source scanner — if "ThreadError" is present, threading is in use.

The flag must be appended **after** the source file in the gcc/clang argv, because `-l` flags are position-sensitive when linking. The `build_gcc_argv` function currently puts `extra_flags` before the source file, so we need a separate mechanism.

- [ ] **Step 1: Add `needs_pthread` field to `state` struct**

In `src/tess/src/tess_exe.c`, add a field to the `state` struct (around line 67, near the other boolean flags):

```c
int               needs_pthread;
```

- [ ] **Step 2: Detect threading imports after `files_in_order()`**

In the `compile()` function, after `files_in_order()` is called (line 1183) and before the parsing phase, add detection:

```c
    // Detect threading library usage for -lpthread linking
#ifndef MOS_WINDOWS
    self->needs_pthread = str_map_contains(self->scanner.modules_seen, S("ThreadError"));
#endif
```

- [ ] **Step 3: Add `-lpthread` to `build_gcc_argv` output**

In `build_gcc_argv()`, just before the NULL terminator is pushed (line 1484), add:

```c
#ifndef MOS_WINDOWS
    if (self->needs_pthread) {
        char const *_t = "-lpthread";
        array_push(argv, _t);
    }
#endif
```

This places `-lpthread` after the source file, which is the correct position for linker library flags.

- [ ] **Step 4: Rebuild the compiler**

Run: `make -j all`
Expected: Clean build.

- [ ] **Step 5: Verify `-lpthread` is added when ThreadError is imported**

Create a temporary test file and compile with `-v` to see the gcc command:

```bash
echo '#module main
#import <ThreadError.tl>
main() { 0 }' > /tmp/test_pthread_flag.tl
./tess exe -v /tmp/test_pthread_flag.tl -o /tmp/test_pthread_flag
```

Expected: The verbose output shows `-lpthread` in the gcc invocation.

- [ ] **Step 6: Verify `-lpthread` is NOT added when ThreadError is not imported**

```bash
echo '#module main
main() { 0 }' > /tmp/test_no_pthread.tl
./tess exe -v /tmp/test_no_pthread.tl -o /tmp/test_no_pthread
```

Expected: No `-lpthread` in the gcc invocation.

- [ ] **Step 7: Commit**

```bash
git add src/tess/src/tess_exe.c
git commit -m "add automatic -lpthread linking when threading modules are imported"
```

---

### Task 3: Thread module

**Files:**
- Create: `src/tl/std/Thread.tl`
- Create: `src/tess/tl/test/pass/test_thread.tl`

- [ ] **Step 1: Write the test file**

```tl
#module main
#import <Thread.tl>

// Test data for spawn/2
_ThreadArg: { value: CInt }

_thread_fn(arg) {
    arg->value = 42
    c_NULL
}

test_spawn_join() {
    fails := 0

    arg := _ThreadArg(value = 0)
    t: Ok := Thread.spawn(_thread_fn, arg.&) else {
        c_printf(c"FAIL spawn\n")
        return 1
    }

    _r := Thread.join(t.v)

    if arg.value != 42 {
        c_printf(c"FAIL value not set by thread: %d\n", arg.value)
        fails += 1
    }

    fails
}

test_spawn_closure() {
    fails := 0
    done: CInt := 0
    done_p := done.&

    t: Ok := Thread.spawn([[alloc, capture(done_p)]] () { done_p.* = 1 }) else {
        c_printf(c"FAIL spawn closure\n")
        return 1
    }

    _r := Thread.join(t.v)

    if done != 1 {
        c_printf(c"FAIL closure did not run\n")
        fails += 1
    }

    fails
}

test_current_eq() {
    fails := 0
    me := Thread.current()

    if !Thread.eq(me, me) {
        c_printf(c"FAIL current thread not equal to itself\n")
        fails += 1
    }

    fails
}

test_detach() {
    fails := 0
    done: CInt := 0
    done_p := done.&

    t: Ok := Thread.spawn([[alloc, capture(done_p)]] () { done_p.* = 1 }) else {
        c_printf(c"FAIL spawn for detach\n")
        return 1
    }

    _d := Thread.detach(t.v)

    // Give the detached thread time to run
    Thread.sleep(0, 100000000)  // 100ms

    if done != 1 {
        c_printf(c"FAIL detached thread did not run\n")
        fails += 1
    }

    fails
}

test_yield() {
    // Just verify it doesn't crash
    Thread.yield()
    0
}

test_sleep() {
    // Verify sleep doesn't crash and takes approximately the right time
    Thread.sleep(0, 10000000)  // 10ms
    0
}

main() {
    fails := 0
    fails += test_spawn_join()
    fails += test_spawn_closure()
    fails += test_detach()
    fails += test_current_eq()
    fails += test_yield()
    fails += test_sleep()

    if fails == 0 { c_printf(c"All thread tests passed.\n") }
    else { c_printf(c"%d thread test(s) failed.\n", fails) }
    fails
}
```

- [ ] **Step 2: Verify the test does not compile yet**

Run: `./tess run src/tess/tl/test/pass/test_thread.tl`
Expected: Compile error (Thread module not found).

- [ ] **Step 3: Create `Thread.tl` with platform C helpers**

Create `src/tl/std/Thread.tl`. The file structure:

1. `#ifc` block with:
   - Platform includes (`<pthread.h>` or `<windows.h>`)
   - `TL_THREAD_SIZE` macro via `sizeof`
   - `tl_thread_spawn` — C helper wrapping `pthread_create` / `CreateThread`
   - `tl_thread_spawn_closure` — C trampoline for closures: accepts `tl_closure*`, invokes `fn(ctx)`, frees the closure struct
   - `tl_thread_join` — wraps `pthread_join` / `WaitForSingleObject`
   - `tl_thread_detach` — wraps `pthread_detach` / `CloseHandle`
   - `tl_thread_yield` — wraps `sched_yield` / `SwitchToThread`
   - `tl_thread_sleep` — wraps `nanosleep` / `Sleep`
   - `tl_thread_current` — wraps `pthread_self` / `GetCurrentThreadId`
   - `tl_thread_equal` — wraps `pthread_equal` / ID comparison

2. `c_` FFI declarations for all the above

3. Tess type:
   ```tl
   T: { _handle: CArray[Byte, c_TL_THREAD_SIZE] }
   ```

4. API functions wrapping `c_` calls with `Result` returns

Key implementation details for the C helpers:

**pthreads trampoline for spawn/2 (generic):**
The generic `spawn[T]` monomorphizes, so each specialization generates a unique C function. However, the user's function has signature `T*(*)(void*)` (i.e., `Ptr[T] -> Ptr[any]`), not `void*(*)(void*)` as `pthread_create` requires. The C helper `tl_thread_spawn` must cast the function pointer: `pthread_create(thread_buf, NULL, (void*(*)(void*))fn, arg)`. This cast is safe because `T*` and `void*` have the same representation on all target platforms.

**pthreads trampoline for spawn/1 (allocated closure):**
The caller must pass an allocated closure (`[[alloc, capture(...)]]`). The trampoline invokes the closure, then frees both the ctx (the captured variables) and the `tl_closure` wrapper.
```c
static void *tl_thread_closure_trampoline(void *arg) {
    tl_closure *cl = (tl_closure *)arg;
    ((void (*)(void *))cl->fn)(cl->ctx);
    free(cl->ctx);
    free(cl);
    return NULL;
}

static int tl_thread_spawn_closure(void *thread_buf, tl_closure closure) {
    tl_closure *cl = (tl_closure *)malloc(sizeof(tl_closure));
    if (!cl) return ENOMEM;
    *cl = closure;
    return pthread_create((pthread_t *)thread_buf, NULL,
                          tl_thread_closure_trampoline, cl);
}
```

**Windows trampoline:**
```c
static DWORD WINAPI tl_thread_closure_trampoline_win(LPVOID arg) {
    tl_closure *cl = (tl_closure *)arg;
    ((void (*)(void *))cl->fn)(cl->ctx);
    free(cl->ctx);
    free(cl);
    return 0;
}
```

**Windows spawn:**
```c
static int tl_thread_spawn_win(void *thread_buf, LPTHREAD_START_ROUTINE fn, void *arg) {
    HANDLE h = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (!h) return GetLastError();
    memcpy(thread_buf, &h, sizeof(HANDLE));
    return 0;
}
```

- [ ] **Step 4: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_thread.tl`
Expected: "All thread tests passed." with exit code 0.

- [ ] **Step 5: Run full test suite**

Run: `make -j test`
Expected: All tests pass (including the new one, which is auto-discovered).

- [ ] **Step 6: Commit**

```bash
git add src/tl/std/Thread.tl src/tess/tl/test/pass/test_thread.tl
git commit -m "add Thread module: spawn, join, detach, yield, sleep"
```

---

### Task 4: Mutex module

**Files:**
- Create: `src/tl/std/Mutex.tl`
- Create: `src/tess/tl/test/pass/test_mutex.tl`

- [ ] **Step 1: Write the test file**

```tl
#module main
#import <Thread.tl>
#import <Mutex.tl>

test_lock_unlock() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    _l := Mutex.lock(mtx.v.&)
    _u := Mutex.unlock(mtx.v.&)
    Mutex.destroy(mtx.v.&)

    fails
}

test_try_lock() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    got: Ok := Mutex.try_lock(mtx.v.&) else {
        c_printf(c"FAIL try_lock\n")
        return 1
    }

    if !got.v {
        c_printf(c"FAIL try_lock should succeed on unlocked mutex\n")
        fails += 1
    }

    _u := Mutex.unlock(mtx.v.&)
    Mutex.destroy(mtx.v.&)

    fails
}

test_recursive() {
    fails := 0

    mtx: Ok := Mutex.init(Mutex.Recursive) else {
        c_printf(c"FAIL recursive mutex init\n")
        return 1
    }

    _l1 := Mutex.lock(mtx.v.&)
    _l2 := Mutex.lock(mtx.v.&)   // should not deadlock
    _u1 := Mutex.unlock(mtx.v.&)
    _u2 := Mutex.unlock(mtx.v.&)
    Mutex.destroy(mtx.v.&)

    fails
}

test_contention() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    counter: CInt := 0
    iterations: CInt := 10000
    counter_p := counter.&
    mtx_p := mtx.v.&

    t1: Ok := Thread.spawn([[alloc, capture(counter_p, mtx_p, iterations)]] () {
        i: CInt := 0
        while i < iterations {
            _l := Mutex.lock(mtx_p)
            counter_p.* += 1
            _u := Mutex.unlock(mtx_p)
            i += 1
        }
    }) else {
        c_printf(c"FAIL spawn t1\n")
        return 1
    }

    t2: Ok := Thread.spawn([[alloc, capture(counter_p, mtx_p, iterations)]] () {
        i: CInt := 0
        while i < iterations {
            _l := Mutex.lock(mtx_p)
            counter_p.* += 1
            _u := Mutex.unlock(mtx_p)
            i += 1
        }
    }) else {
        c_printf(c"FAIL spawn t2\n")
        return 1
    }

    _j1 := Thread.join(t1.v)
    _j2 := Thread.join(t2.v)

    if counter != iterations * 2 {
        c_printf(c"FAIL counter %d != %d\n", counter, iterations * 2)
        fails += 1
    }

    Mutex.destroy(mtx.v.&)

    fails
}

main() {
    fails := 0
    fails += test_lock_unlock()
    fails += test_try_lock()
    fails += test_recursive()
    fails += test_contention()

    if fails == 0 { c_printf(c"All mutex tests passed.\n") }
    else { c_printf(c"%d mutex test(s) failed.\n", fails) }
    fails
}
```

- [ ] **Step 2: Verify the test does not compile yet**

Run: `./tess run src/tess/tl/test/pass/test_mutex.tl`
Expected: Compile error (Mutex module not found).

- [ ] **Step 3: Create `Mutex.tl`**

Create `src/tl/std/Mutex.tl`. Structure:

1. `#ifc` block with:
   - Platform includes
   - `TL_MUTEX_SIZE` macro via `sizeof(pthread_mutex_t)` / `sizeof(CRITICAL_SECTION)`
   - C helpers:
     - `tl_mutex_init(void *buf, int kind)` — `pthread_mutex_init` with attributes / `InitializeCriticalSection`
     - `tl_mutex_lock(void *buf)` — `pthread_mutex_lock` / `EnterCriticalSection`
     - `tl_mutex_unlock(void *buf)` — `pthread_mutex_unlock` / `LeaveCriticalSection`
     - `tl_mutex_trylock(void *buf)` — `pthread_mutex_trylock` / `TryEnterCriticalSection`. Returns 1 if acquired, 0 if not, negative on error.
     - `tl_mutex_destroy(void *buf)` — `pthread_mutex_destroy` / `DeleteCriticalSection`

2. `c_` FFI declarations

3. Types:
   ```tl
   Kind: | Plain
         | Recursive
         | ErrorCheck

   T: { _handle: CArray[Byte, c_TL_MUTEX_SIZE]
        _kind: Kind }
   ```

4. API functions:
   - `init(kind: Kind)` and `init()` — create mutex, zero-init with `c_memset`, call `c_tl_mutex_init`
   - `lock`, `unlock` — call C helpers, wrap errors in `Result`
   - `try_lock` — call C helper, return `Result[Bool, ThreadError]`
   - `destroy` — call C helper

**pthreads kind mapping:**
```c
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
```

**Windows kind handling:**
Windows `CRITICAL_SECTION` is always recursive. For `Plain` and `ErrorCheck` kinds, the C helpers must track the owning thread ID and lock count to emulate non-recursive / error-checking behavior. This tracking state can be stored in a separate struct embedded in the Tess-side `_handle` buffer, or managed alongside `CRITICAL_SECTION`. The simplest approach: on Windows, `TL_MUTEX_SIZE` is `sizeof(CRITICAL_SECTION) + sizeof(DWORD) + sizeof(int)` (thread ID + lock count), and the helpers manage this state.

- [ ] **Step 4: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_mutex.tl`
Expected: "All mutex tests passed." with exit code 0.

- [ ] **Step 5: Run full test suite**

Run: `make -j test`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/tl/std/Mutex.tl src/tess/tl/test/pass/test_mutex.tl
git commit -m "add Mutex module: plain, recursive, error-checking mutexes"
```

---

### Task 5: Cond module

**Files:**
- Create: `src/tl/std/Cond.tl`
- Create: `src/tess/tl/test/pass/test_cond.tl`

- [ ] **Step 1: Write the test file**

A producer-consumer test: one thread produces a value, signals the condvar; the main thread waits for the signal.

```tl
#module main
#import <Thread.tl>
#import <Mutex.tl>
#import <Cond.tl>

test_signal_wait() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }
    cv: Ok := Cond.init() else {
        c_printf(c"FAIL cond init\n")
        return 1
    }

    ready: CInt := 0
    value: CInt := 0
    ready_p := ready.&
    value_p := value.&
    mtx_p := mtx.v.&
    cv_p := cv.v.&

    // Producer thread
    t: Ok := Thread.spawn([[alloc, capture(ready_p, value_p, mtx_p, cv_p)]] () {
        _l := Mutex.lock(mtx_p)
        value_p.* = 42
        ready_p.* = 1
        _s := Cond.signal(cv_p)
        _u := Mutex.unlock(mtx_p)
    }) else {
        c_printf(c"FAIL spawn\n")
        return 1
    }

    // Consumer (main thread)
    _l := Mutex.lock(mtx_p)
    while ready == 0 {
        _w := Cond.wait(cv_p, mtx_p)
    }
    if value != 42 {
        c_printf(c"FAIL value %d != 42\n", value)
        fails += 1
    }
    _u := Mutex.unlock(mtx_p)

    _j := Thread.join(t.v)
    Cond.destroy(cv_p)
    Mutex.destroy(mtx_p)

    fails
}

test_broadcast() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }
    cv: Ok := Cond.init() else {
        c_printf(c"FAIL cond init\n")
        return 1
    }

    go: CInt := 0
    count: CInt := 0
    go_p := go.&
    count_p := count.&
    mtx_p := mtx.v.&
    cv_p := cv.v.&

    // Two waiting threads
    t1: Ok := Thread.spawn([[alloc, capture(go_p, count_p, mtx_p, cv_p)]] () {
        _l := Mutex.lock(mtx_p)
        while go_p.* == 0 {
            _w := Cond.wait(cv_p, mtx_p)
        }
        count_p.* += 1
        _u := Mutex.unlock(mtx_p)
    }) else {
        c_printf(c"FAIL spawn t1\n")
        return 1
    }

    t2: Ok := Thread.spawn([[alloc, capture(go_p, count_p, mtx_p, cv_p)]] () {
        _l := Mutex.lock(mtx_p)
        while go_p.* == 0 {
            _w := Cond.wait(cv_p, mtx_p)
        }
        count_p.* += 1
        _u := Mutex.unlock(mtx_p)
    }) else {
        c_printf(c"FAIL spawn t2\n")
        return 1
    }

    // Give threads time to start waiting
    Thread.sleep(0, 50000000)  // 50ms

    // Broadcast
    _l := Mutex.lock(mtx_p)
    go = 1
    _b := Cond.broadcast(cv_p)
    _u := Mutex.unlock(mtx_p)

    _j1 := Thread.join(t1.v)
    _j2 := Thread.join(t2.v)

    if count != 2 {
        c_printf(c"FAIL broadcast count %d != 2\n", count)
        fails += 1
    }

    Cond.destroy(cv_p)
    Mutex.destroy(mtx_p)

    fails
}

main() {
    fails := 0
    fails += test_signal_wait()
    fails += test_broadcast()

    if fails == 0 { c_printf(c"All cond tests passed.\n") }
    else { c_printf(c"%d cond test(s) failed.\n", fails) }
    fails
}
```

- [ ] **Step 2: Verify the test does not compile yet**

Run: `./tess run src/tess/tl/test/pass/test_cond.tl`
Expected: Compile error (Cond module not found).

- [ ] **Step 3: Create `Cond.tl`**

Create `src/tl/std/Cond.tl`. Structure:

1. `#ifc` block with:
   - Platform includes
   - `TL_COND_SIZE` macro via `sizeof(pthread_cond_t)` / `sizeof(CONDITION_VARIABLE)`
   - C helpers:
     - `tl_cond_init(void *buf)` — `pthread_cond_init` / `InitializeConditionVariable`
     - `tl_cond_wait(void *cond_buf, void *mtx_buf)` — `pthread_cond_wait` / `SleepConditionVariableCS`
     - `tl_cond_signal(void *buf)` — `pthread_cond_signal` / `WakeConditionVariable`
     - `tl_cond_broadcast(void *buf)` — `pthread_cond_broadcast` / `WakeAllConditionVariable`
     - `tl_cond_destroy(void *buf)` — `pthread_cond_destroy` / no-op on Windows

2. `c_` FFI declarations

3. Type:
   ```tl
   T: { _handle: CArray[Byte, c_TL_COND_SIZE] }
   ```

4. API functions wrapping C helpers with `Result` returns

**Note on `Cond.wait`:** The function takes `Ptr[Mutex]`, but the C helper needs the raw mutex buffer. The Tess wrapper passes `mtx->_handle.&` to the C helper. On Windows, `SleepConditionVariableCS` takes a `PCRITICAL_SECTION`, which is the `_handle` field of Mutex.

- [ ] **Step 4: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_cond.tl`
Expected: "All cond tests passed." with exit code 0.

- [ ] **Step 5: Run full test suite**

Run: `make -j test`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/tl/std/Cond.tl src/tess/tl/test/pass/test_cond.tl
git commit -m "add Cond module: condition variables with wait, signal, broadcast"
```

---

### Task 6: Once module

**Files:**
- Create: `src/tl/std/Once.tl`
- Create: `src/tess/tl/test/pass/test_once.tl`

- [ ] **Step 1: Write the test file**

```tl
#module main
#import <Thread.tl>
#import <Once.tl>

test_once_single_thread() {
    fails := 0
    counter: CInt := 0
    counter_p := counter.&
    once := Once.init()
    once_p := once.&

    Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })
    Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })
    Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })

    if counter != 1 {
        c_printf(c"FAIL counter %d != 1\n", counter)
        fails += 1
    }

    fails
}

test_once_multi_thread() {
    fails := 0
    counter: CInt := 0
    counter_p := counter.&
    once := Once.init()
    once_p := once.&

    t1: Ok := Thread.spawn([[alloc, capture(counter_p, once_p)]] () {
        Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })
    }) else {
        c_printf(c"FAIL spawn t1\n")
        return 1
    }

    t2: Ok := Thread.spawn([[alloc, capture(counter_p, once_p)]] () {
        Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })
    }) else {
        c_printf(c"FAIL spawn t2\n")
        return 1
    }

    t3: Ok := Thread.spawn([[alloc, capture(counter_p, once_p)]] () {
        Once.call(once_p, [[alloc, capture(counter_p)]] () { counter_p.* += 1 })
    }) else {
        c_printf(c"FAIL spawn t3\n")
        return 1
    }

    _j1 := Thread.join(t1.v)
    _j2 := Thread.join(t2.v)
    _j3 := Thread.join(t3.v)

    if counter != 1 {
        c_printf(c"FAIL multi-thread counter %d != 1\n", counter)
        fails += 1
    }

    fails
}

main() {
    fails := 0
    fails += test_once_single_thread()
    fails += test_once_multi_thread()

    if fails == 0 { c_printf(c"All once tests passed.\n") }
    else { c_printf(c"%d once test(s) failed.\n", fails) }
    fails
}
```

- [ ] **Step 2: Verify the test does not compile yet**

Run: `./tess run src/tess/tl/test/pass/test_once.tl`
Expected: Compile error (Once module not found).

- [ ] **Step 3: Create `Once.tl`**

Create `src/tl/std/Once.tl`. Structure:

1. `#ifc` block with:
   - Platform includes
   - `TL_ONCE_SIZE` macro via `sizeof(pthread_once_t)` / `sizeof(INIT_ONCE)`
   - On pthreads: a `TL_THREAD_LOCAL tl_closure *` variable for passing the closure through `pthread_once`
   - C helpers:
     - `tl_once_init(void *buf)` — zero-init (equivalent to `PTHREAD_ONCE_INIT`)
     - `tl_once_call(void *buf, tl_closure closure)` — see below

   **pthreads `tl_once_call`:**
   Use `_Thread_local` (C11 keyword) in the `#ifc` block — not `__thread` (GCC extension) and not `TL_THREAD_LOCAL` (which is defined in `embed/std.c`, not available in `#ifc` blocks). On MSVC, this code path is not reached (Windows uses `InitOnceExecuteOnce`).
   ```c
   static _Thread_local tl_closure *tl_once_current_closure;

   static void tl_once_trampoline(void) {
       tl_closure *cl = tl_once_current_closure;
       ((void (*)(void *))cl->fn)(cl->ctx);
   }

   static void tl_once_call(void *buf, tl_closure closure) {
       tl_once_current_closure = &closure;
       pthread_once((pthread_once_t *)buf, tl_once_trampoline);
   }
   ```

   **Windows `tl_once_call`:**
   ```c
   static BOOL CALLBACK tl_once_trampoline_win(PINIT_ONCE once, PVOID param, PVOID *ctx) {
       tl_closure *cl = (tl_closure *)param;
       ((void (*)(void *))cl->fn)(cl->ctx);
       return TRUE;
   }

   static void tl_once_call(void *buf, tl_closure closure) {
       InitOnceExecuteOnce((PINIT_ONCE)buf, tl_once_trampoline_win, &closure, NULL);
   }
   ```

2. `c_` FFI declarations

3. Type:
   ```tl
   T: { _handle: CArray[Byte, c_TL_ONCE_SIZE] }
   ```

4. API:
   - `init() -> Once` — creates a Once, zero-inits with `c_memset`
   - `call(self: Ptr[Once], fn: () -> Void) -> Void` — calls `c_tl_once_call`

- [ ] **Step 4: Run the test**

Run: `./tess run src/tess/tl/test/pass/test_once.tl`
Expected: "All once tests passed." with exit code 0.

- [ ] **Step 5: Run full test suite**

Run: `make -j test`
Expected: All tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/tl/std/Once.tl src/tess/tl/test/pass/test_once.tl
git commit -m "add Once module: one-time initialization"
```

---

### Task 7: Final verification

- [ ] **Step 1: Full test suite**

Run: `make -j test`
Expected: All tests pass, including all four new test files.

- [ ] **Step 2: Debug build test**

Run: `make CONFIG=debug clean && make CONFIG=debug -j all && make CONFIG=debug -j test`
Expected: All tests pass under debug build (catches 0xCD uninitialized memory bugs).

- [ ] **Step 3: Verify formatting**

Run: `./tess fmt src/tl/std/ThreadError.tl && ./tess fmt src/tl/std/Thread.tl && ./tess fmt src/tl/std/Mutex.tl && ./tess fmt src/tl/std/Cond.tl && ./tess fmt src/tl/std/Once.tl`
Expected: No changes (files are already formatted).

- [ ] **Step 4: Commit any formatting fixes**

If `tess fmt` made changes, commit them:
```bash
git add src/tl/std/*.tl
git commit -m "formatting"
```
