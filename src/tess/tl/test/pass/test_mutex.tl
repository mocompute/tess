#module main
#import <Thread.tl>
#import <Mutex.tl>

test_lock_unlock() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    _l := Mutex.lock(mtx.value.&)
    _u := Mutex.unlock(mtx.value.&)
    Mutex.destroy(mtx.value.&)

    fails
}

test_try_lock() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    got: Ok := Mutex.try_lock(mtx.value.&) else {
        c_printf(c"FAIL try_lock\n")
        return 1
    }

    if !got.value {
        c_printf(c"FAIL try_lock should succeed on unlocked mutex\n")
        fails += 1
    }

    _u := Mutex.unlock(mtx.value.&)
    Mutex.destroy(mtx.value.&)

    fails
}

test_recursive() {
    fails := 0

    mtx: Ok := Mutex.init(Mutex.Recursive) else {
        c_printf(c"FAIL recursive mutex init\n")
        return 1
    }

    _l1 := Mutex.lock(mtx.value.&)
    _l2 := Mutex.lock(mtx.value.&)      // should not deadlock
    _u1 := Mutex.unlock(mtx.value.&)
    _u2 := Mutex.unlock(mtx.value.&)
    Mutex.destroy(mtx.value.&)

    fails
}

test_contention() {
    fails := 0

    mtx: Ok := Mutex.init() else {
        c_printf(c"FAIL mutex init\n")
        return 1
    }

    counter:    CInt := 0
    iterations: CInt := 10000
    counter_p        := counter.&
    mtx_p            := mtx.value.&

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

    _j1 := Thread.join(t1.value)
    _j2 := Thread.join(t2.value)

    if counter != iterations * 2 {
        c_printf(c"FAIL counter %d != %d\n", counter, iterations * 2)
        fails += 1
    }

    Mutex.destroy(mtx.value.&)

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
