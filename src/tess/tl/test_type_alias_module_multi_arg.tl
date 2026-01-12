#import "builtin.tl"

#module Collections
Map(K, V) : { keys: Ptr(K), values: Ptr(V), size: Int }

#module App
StringIntMap = Collections.Map(Ptr(CChar), Int)

#module main
main() {
    m : App.StringIntMap := Collections.Map(keys = null, values = null, size = 0)
    m.size
}
