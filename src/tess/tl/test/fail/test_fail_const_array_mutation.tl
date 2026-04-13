#module main

Array[T]: {
    data:     Ptr[T],
    size:     CSize,
    capacity: CSize,
}

Point: { x: Int, y: Int }

(arr: Ptr[Const[Array[T]]]): {

    swap(i, j) {
        if i >= arr->size { _tl_fatal_(c"Array.swap: index out of bounds") }
        if j >= arr->size { _tl_fatal_(c"Array.swap: index out of bounds") }
        tmp := arr->data.[i]
        arr->data.[i] = arr->data.[j]
        arr->data.[j] = tmp
    }

    reverse() {
        if arr->size == 0 { return void }
        i: CSize := 0
        j        := arr->size - 1
        while i < j, i += 1 {
            arr.swap(i, j)
            j -= 1
        }
    }


}

mutate_array(arr: Ptr[Const[Array]]) {
    arr.reverse()
}

main() {
    arr := Array (data: Ptr[Point] = null, capacity = 0, size = 0)
    mutate_array(arr.&)
    0
}
