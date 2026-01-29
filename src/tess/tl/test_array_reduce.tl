#module main
#import <Array.tl>

main() {
  error := 0
  arr := Array.empty(Int)
  Array.push(arr.&, 1)
  Array.push(arr.&, 2)
  Array.push(arr.&, 3)
  Array.push(arr.&, 4)
  sum := Array.reduce(arr, 0, (acc, x) { acc + x })
  if sum != 10           { error += 1 }
  // With initial value
  sum2 := Array.reduce(arr, 100, (acc, x) { acc + x })
  if sum2 != 110         { error += 1 }
  // Empty array
  e := Array.empty(Int)
  sum3 := Array.reduce(e, 42, (acc, x) { acc + x })
  if sum3 != 42          { error += 1 }
  Array.free(arr.&)
  error
}
