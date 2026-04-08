#module Json
#import <Alloc.tl>
#import <Array.tl>
#import <HashMap.tl>
#import "json/json_internal.tl"

Allocator = Alloc.Allocator

// RFC 8259

Value: | JObject { value: HashMap[String, Ptr[Value]] }
       | JArray  { value: Array[Ptr[Value]] }
       | JString { value: String }
       | JInt    { value: CInt64 }
       | JDouble { value: CDouble }
       | JTrue
       | JFalse
       | JNull


#module Json.Context

Allocator = Alloc.Allocator

Context: {
    alloc: Ptr[Allocator]
}


(): {
    init(alloc: Ptr[Allocator]) {
        Context(alloc = alloc)
    }

    create(alloc: Ptr[Allocator]) {
        ctx: Ptr[Context] := alloc.alloc(sizeof[Context]())
        ctx.* = init(alloc)
        ctx
    }
}

(self: Ptr[Context]): {

    destroy() {
        self->alloc.dealloc(self)
    }

}
