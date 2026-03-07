#module Collections
Map(K, V): { keys: Ptr[K], values: Ptr[V], size: Int }

#module App
// Aliases which partially specialize a generic type are not supported. This line generates an error:
StringMap(V) = Collections.Map(CString, V)

#module main

main() {
    m: App.StringMap := Collections.Map(keys = null, values = null, size = 0)
    m.size
}
