#module Collections
Map[K, V]: { keys: Ptr[K], values: Ptr[V], size: Int }

#module App
StringIntMap = Collections.Map[CString, Int]

#module main

main() {
    // Via type annotation: construct using original type, annotate with alias
    m1: App.StringIntMap := Collections.Map(keys = null, values = null, size = 0)

    // Direct usage: construct using multi-argument generic alias
    m2 := App.StringIntMap(keys = null, values = null, size = 0)

    result : CInt := m1.size + m2.size
    result
}
