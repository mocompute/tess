# Command-Line Arguments

This example shows how to access command-line arguments (`argc` and `argv`)
in a Tess program.

## Source

```tess
#module main
#import <cstdio.tl>

main(argc: Int, argv: Ptr[CString]) {
    c_printf("argc = %i\n", argc)

    i := 0
    while i < argc, i = i + 1 {
        c_printf("%s\n", argv.[i])
    }

    0
}
```

## Building and running

Compile to an executable:

```bash
tess exe -o example argc_argv.tl
./example hello world
```

Or run directly:

```bash
tess run argc_argv.tl -- hello world
```

Expected output:

```
argc = 3
./example
hello
world
```
