#module Collections
Map(K, V) : { keys: Ptr(K), values: Ptr(V), size: Int }

#module App
StringIntMap = Collections.Map(CString, Int)

#module main
main() {
    // Direct usage: construct using multi-argument generic alias
    m := App.StringIntMap(keys = null, values = null, size = 0)
    m.size
}
