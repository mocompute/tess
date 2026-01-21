#module main
#import <Alloc.tl>


main() {
  error := 0
  // Note: must use intrinsic because c_max_align_t is a C typedef
  max_align := _tl_sizeof_(c_max_align_t)

  error += if sizeof(Ptr(Void)) <= max_align {0} else {1}
  error += if sizeof(Ptr(Void)) >= sizeof(CSize) {0} else {1}

  if sizeof(Ptr(Void)) < max_align {
    error += if Alloc.align_max(sizeof(Ptr(Void)) + 1) == max_align {0} else {1}
  }
  else {
    error += if Alloc.align_max(max_align+1) == 2 * max_align {0} else {1}
  }
  error
}
