#module main
#import <Array.tl>


main() {
    sum := 0

    arr := Array.with_capacity[Int](4)
    arr.push(1)
    arr.push(2)
    arr.push(3)

    for x in arr {
        narrow: CInt := x
        sum += narrow
    }


    sum - 6

}
