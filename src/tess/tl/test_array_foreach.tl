#module main
#import <Array.tl>

main() {
  error := 0
  arr := Array.empty(Int)
  Array.push(arr.&, 1)
  Array.push(arr.&, 2)
  Array.push(arr.&, 3)
  count := 0
  Array.foreach(arr, (x) { count = count + 1 })
  if count != 3 { error += 1 }
  Array.free(arr.&)
  error
}
