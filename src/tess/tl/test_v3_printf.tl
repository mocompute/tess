#module main
#import <stdio.tl>

main(argc: Int, argv: Ptr(String)) {
  c_printf("argc = %i\n", argc)
  argc - 1
}
