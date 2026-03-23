#module main
#import <Thread.tl>
#import <Once.tl>

test_once_single_thread() {
    fails         := 0
    counter: CInt := 0
    counter_p     := counter.&
    once          := Once.init()
    once_p        := once.&

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
    fails         := 0
    counter: CInt := 0
    counter_p     := counter.&
    once          := Once.init()
    once_p        := once.&

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
