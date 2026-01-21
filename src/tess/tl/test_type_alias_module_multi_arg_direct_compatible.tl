#module Collections
Map(K, V) : { keys: Ptr(K), values: Ptr(V), size: Int }

#module App
StringIntMap = Collections.Map(CString, Int)

#module main
main() {
    // Direct usage: construct using multi-argument generic alias
    m := App.StringIntMap(keys = null, values = null, size = 0)
    m2 := Collections.Map(keys: Ptr(CString) = null, values: Ptr(Int) = null, size = 0)
    m = m2
    m.size
}
