// From root directory, build with:
// ./tess exe -I src/tl/std -o example src/tl/examples/argc_argv.tl

#module main
#import <stdio.tl>

main(argc: Int, argv: Ptr(String)) {
  c_printf("argc = %i\n", argc)

  i := 0
  while i < argc, i = i + 1 {
    c_printf("%s\n", argv[i])
  }

  0
}
