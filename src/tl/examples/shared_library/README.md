# Shared Library Example

This example shows how to compile a Tess module into a shared library (`.so`)
and call it from a C program.

## Tess source

The library source is in `src/lib.tl`:

```tess
#module MyLib

[[c_export]] addi(x: CInt, y: CInt)       { x + y }
[[c_export]] addf(x: CDouble, y: CDouble) { x + y }
```

- `#module MyLib` declares the module name. Exported symbols are prefixed with
  `MyLib_` (e.g. `MyLib_addi`).
- `[[c_export]]` marks a function for export in the shared library. You can
  also specify an explicit C name: `[[c_export("add_ints")]]` will export the
  function as `add_ints` instead of the default `MyLib_addi`.
- Parameters use C-compatible types (`CInt`, `CDouble`) so the generated
  symbols have standard C calling conventions.

If you don't want a module prefix on the exported names, you can use
`#module main` instead. The `main` module is the global namespace, so functions
in it are exported without any prefix — `addi` rather than `MyLib_addi`.

## Building the library

From this directory, run:

```bash
tess lib -o mylib.so
```

This produces two files:

| File       | Description                          |
|------------|--------------------------------------|
| `mylib.so` | The shared library                   |
| `mylib.h`  | A C header declaring exported symbols|

The generated header looks like:

```c
void tl_init_mylib(void);
double MyLib_addf(double, double);
int MyLib_addi(int, int);
```

## The init function

Every Tess shared library exposes an init function named `tl_init_<library>()`.
You **must** call it once before calling any exported function. It initializes
the Tess runtime (allocator, string table, etc.).

## Calling from C

`main.c` is a minimal consumer:

```c
#include <stdio.h>
#include "mylib.h"

int main(void)
{
    tl_init_mylib();

    printf("addi(2, 3) = %d\n", MyLib_addi(2, 3));
    printf("addf(1.5, 2.5) = %f\n", MyLib_addf(1.5, 2.5));

    return 0;
}
```

Compile and run:

```bash
cc main.c ./mylib.so -o main
LD_LIBRARY_PATH=. ./main
```

Expected output:

```
addi(2, 3) = 5
addf(1.5, 2.5) = 4.000000
```
