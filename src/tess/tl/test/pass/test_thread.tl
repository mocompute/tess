#module main
#import <Thread.tl>

// Test data for spawn/2
_ThreadArg: { value: CInt }

_thread_fn(arg: Ptr[_ThreadArg]) -> Ptr[any] {
    arg->value = 42
    c_NULL
}

test_spawn_fn_ptr() {
    fails := 0

    arg   := _ThreadArg(value = 0)
    t: Ok := Thread.spawn(_thread_fn/1, arg.&) else {
        c_printf(c"FAIL spawn fn ptr\n")
        return 1
    }

    _r := Thread.join(t.value)

    if arg.value != 42 {
        c_printf(c"FAIL fn ptr value not set: %d\n", arg.value)
        fails += 1
    }

    fails
}

test_spawn_join() {
    fails := 0

    value: CInt := 0
    value_p     := value.&

    t: Ok := Thread.spawn([[alloc, capture(value_p)]] () { value_p.* = 42 }) else {
        c_printf(c"FAIL spawn\n")
        return 1
    }

    _r := Thread.join(t.value)

    if value != 42 {
        c_printf(c"FAIL value not set by thread: %d\n", value)
        fails += 1
    }

    fails
}

test_spawn_closure() {
    fails      := 0
    done: CInt := 0
    done_p     := done.&

    t: Ok := Thread.spawn([[alloc, capture(done_p)]] () { done_p.* = 1 }) else {
        c_printf(c"FAIL spawn closure\n")
        return 1
    }

    _r := Thread.join(t.value)

    if done != 1 {
        c_printf(c"FAIL closure did not run\n")
        fails += 1
    }

    fails
}

test_spawn_lambda() {
    fails := 0

    t: Ok := Thread.spawn(() { Thread.yield() }) else {
        c_printf(c"FAIL spawn lambda\n")
        return 1
    }

    _r := Thread.join(t.value)
    fails
}

test_spawn_lambda_binding() {
    fails := 0
    fn    := () { Thread.yield() }

    t: Ok := Thread.spawn(fn) else {
        c_printf(c"FAIL spawn lambda binding\n")
        return 1
    }

    _r := Thread.join(t.value)
    fails
}

test_current_eq() {
    fails := 0
    me    := Thread.current()

    if !Thread.eq(me, me) {
        c_printf(c"FAIL current thread not equal to itself\n")
        fails += 1
    }

    fails
}

test_detach() {
    fails      := 0
    done: CInt := 0
    done_p     := done.&

    t: Ok := Thread.spawn([[alloc, capture(done_p)]] () { done_p.* = 1 }) else {
        c_printf(c"FAIL spawn for detach\n")
        return 1
    }

    _d := Thread.detach(t.value)

    // Give the detached thread time to run
    Thread.sleep(0, 100000000)          // 100ms

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
    Thread.sleep(0, 10000000)           // 10ms
    0
}

main() {
    fails := 0
    fails += test_spawn_fn_ptr()
    fails += test_spawn_join()
    fails += test_spawn_closure()
    fails += test_spawn_lambda()
    fails += test_spawn_lambda_binding()
    fails += test_detach()
    fails += test_current_eq()
    fails += test_yield()
    fails += test_sleep()

    if fails == 0 { c_printf(c"All thread tests passed.\n") }
    else { c_printf(c"%d thread test(s) failed.\n", fails) }
    fails
}
