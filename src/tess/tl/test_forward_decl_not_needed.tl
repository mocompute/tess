#module main

// Declaration order does not matter
foo (x) { bar(x)        }
bar (x) { 42            }
main()  { foo(999) - 42 }
