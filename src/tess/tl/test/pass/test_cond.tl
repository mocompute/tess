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
    ready_p     := ready.&
    value_p     := value.&
    mtx_p       := mtx.value.&
    cv_p        := cv.value.&

    // Producer thread
    t: Ok := Thread.spawn([[alloc, capture(ready_p, value_p, mtx_p, cv_p)]] () {
                          _l := Mutex.lock(mtx_p)
                          value_p.* = 42
                          ready_p.* = 1
                          _s := Cond.signal(cv_p)
                          _u := Mutex.unlock(mtx_p)
                          void
                          }) else {
        c_printf(c"FAIL spawn\n")
        return 1
    }

    // Consumer (main thread)
    _l := Mutex.lock(mtx_p)
    while ready == 0 {
        _w := Cond.wait(cv_p, mtx_p)
        void
    }
    if value != 42 {
        c_printf(c"FAIL value %d != 42\n", value)
        fails += 1
    }
    _u := Mutex.unlock(mtx_p)

    _j := Thread.join(t.value)
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

    go:    CInt := 0
    count: CInt := 0
    go_p        := go.&
    count_p     := count.&
    mtx_p       := mtx.value.&
    cv_p        := cv.value.&

    // Two waiting threads
    t1: Ok := Thread.spawn([[alloc, capture(go_p, count_p, mtx_p, cv_p)]] () {
                           _l := Mutex.lock(mtx_p)
                           while go_p.* == 0 {
                           _w := Cond.wait(cv_p, mtx_p)
                           void
                           }
                           count_p.* += 1
                           _u := Mutex.unlock(mtx_p)
                           void
                           }) else {
        c_printf(c"FAIL spawn t1\n")
        return 1
    }

    t2: Ok := Thread.spawn([[alloc, capture(go_p, count_p, mtx_p, cv_p)]] () {
                           _l := Mutex.lock(mtx_p)
                           while go_p.* == 0 {
                           _w := Cond.wait(cv_p, mtx_p)
                           void
                           }
                           count_p.* += 1
                           _u := Mutex.unlock(mtx_p)
                           void
                           }) else {
        c_printf(c"FAIL spawn t2\n")
        return 1
    }

    // Give threads time to start waiting
    Thread.sleep(0, 50000000)           // 50ms

    // Broadcast
    _l := Mutex.lock(mtx_p)
    go = 1
    _b := Cond.broadcast(cv_p)
    _u := Mutex.unlock(mtx_p)

    _j1 := Thread.join(t1.value)
    _j2 := Thread.join(t2.value)

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
