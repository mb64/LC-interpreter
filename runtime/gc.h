#include "runtime.h"

void gc_init(void);

void minor_gc(void);
void write_barrier(obj *thunk);

// This is only called from C code; generated code has this inlined
static inline obj *alloc(void (*entrypoint)(void), size_t size) {
  // TODO: have a max term size somewhere
  assert(sizeof(word[size]) < 10240);
  word *ptr = nursery_top - size;
  if (ptr < nursery_start) {
    minor_gc();
    ptr = nursery_top - size;
  }
  nursery_top = ptr;
  obj *o = (obj *) ptr;
  o->entrypoint = entrypoint;
  return o;
}

