#include "runtime.h"

void gc_init(void);

/** This is only called from C code; generated code hasthese tests inlined */
obj *alloc(void (*entrypoint)(void), size_t size);

void minor_gc(void);
void write_barrier(obj *thunk);

