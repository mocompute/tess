#module main

main(argc: Int, argv: Ptr(String)) {
  c_printf("argc = %i\n", argc)
  i := 0
  while i < argc, i = i + 1 {
    c_printf("%s\n", argv[i])
  }

  0
}
