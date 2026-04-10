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

Value: | JObject     { value: Ptr[HashMap[String, Ptr[Value]]] }
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


// Provides arena-based lifetime for one or more Json objects.
Context: {
    alloc: Ptr[Allocator],

    // pointer identity for these immutable values
    ptrue:  Ptr[J.Value],
    pfalse: Ptr[J.Value],
    pnull:  Ptr[J.Value],

}


(): {
    init(alloc: Ptr[Allocator]) {
        ptrue:  Ptr[J.Value] := alloc.alloc(sizeof[J.Value]())
        pfalse: Ptr[J.Value] := alloc.alloc(sizeof[J.Value]())
        pnull:  Ptr[J.Value] := alloc.alloc(sizeof[J.Value]())
        ptrue.*  = J.JTrue()
        pfalse.* = J.JFalse()
        pnull.*  = J.JNull()
        Context(alloc = alloc, ptrue = ptrue, pfalse = pfalse, pnull = pnull)
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

    create_string(str: Ptr[Const[String]]) {
        pval := self._alloc_value()
        pval.* = J.JString(value = str.copy(self->alloc))
        pval
    }

    create_array() {
        pval := self._alloc_value()
        pval.* = J.JArray(value = Array.empty[Ptr[J.Value]]())
        pval
    }

    create_object() {
        pval := self._alloc_value()
        pval.* = J.JObject(value = HashMap.create[String, Ptr[J.Value]](self->alloc))
        pval

    }

    create_int(x) {
        pval := self._alloc_value()
        pval.* = J.JInt64(x)
        pval
    }

    create_uint(x) {
        pval := self._alloc_value()
        pval.* = J.JUInt64(x)
        pval
    }

    create_double(x) {
        pval := self._alloc_value()
        pval.* = J.JDouble(x)
        pval
    }

    create_long_double(x) {
        pval := self._alloc_value()
        pval.* = J.JLongDouble(x)
        pval
    }

    _alloc_value() -> Ptr[J.Value] { self->alloc.alloc(sizeof[J.Value]()) }
}

(self: Ptr[Const[Context]]): {
    create_true () -> Ptr[J.Value] { self->ptrue  }
    create_false() -> Ptr[J.Value] { self->pfalse }
    create_null () -> Ptr[J.Value] { self->pnull  }
}

(ctx: Ptr[Context], self: Ptr[J.Value]): {

    push(item: Ptr[J.Value]) {
        arr: JArray := self.& else { _tl_fatal_(c"wrong variant") }
        arr->value.push(ctx->alloc, item)
    }

}
