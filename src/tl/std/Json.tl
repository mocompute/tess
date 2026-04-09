#module Json
#import <Alloc.tl>
#import <Array.tl>
#import <HashMap.tl>
#import "json/json_internal.tl"


//
// -- module Json.Value --
//
#module Json.Value

// RFC 8259

Value: | JObject     { value: HashMap[String, Ptr[Value]] }
       | JArray      { value: Array[Ptr[Value]] }
       | JString     { value: String }
       | JInt64      { value: CInt64 }
       | JUInt64     { value: CUInt64 }
       | JDouble     { value: CDouble }
       | JLongDouble { value: CLongDouble }
       | JTrue
       | JFalse
       | JNull


(): {

}


//
// -- module Json.Context --
//
#module Json.Context
Allocator = Alloc.Allocator

#alias J Json.Value

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

    copy_string(str: Ptr[Const[String]]) {
        J.JString(value = str.copy(self->alloc))
    }

    create_array() {
        J.JArray(value = Array.empty[Ptr[Value]]())
    }

}

(ctx: Ptr[Context], self: Ptr[J.Value]): {

    push(item: Ptr[J.Value]) {
        arr: JArray := self.* else { _tl_fatal_(c"wrong variant") }
        arr.value.push(ctx->alloc, item)
    }

}
