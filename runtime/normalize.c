#include "gc.h"
#include "builtins.h"
#include "normalize.h"

static unsigned int *buf;
static size_t buf_len;
static size_t buf_cap;

static void push_buf(unsigned int x) {
  failwith("TODO");
}

// Pop an object from the data stack and write its normal form to the buffer
static void quote(unsigned int lvl);

struct saved_regs {
  obj *self;
  obj **data_stack;
  word *nursery_top;
  word *nursery_start;
  size_t argc;
};

static struct saved_regs save_regs(void);
static void restore_regs(struct saved_regs);

// Caller is normal code, calls the runtime code
// Need to save/restore the callee-saved registers and setup the runtime
unsigned int *normalize(void (*entrypoint)(void)) {
  struct saved_regs regs = save_regs();
  gc_init();
  failwith("TODO init buf");

  obj *main = alloc(entrypoint, 2);
  *INFO_WORD(main) = (struct info_word) { .size = 2, .var = 0 };
  *data_stack-- = main;
  self = main;
  main->entrypoint();
  rt_update_thunk();
  quote(0);

  restore_regs(regs);
  return buf;
}

static struct saved_regs save_regs(void) {
  return (struct saved_regs) {
    .self = self,
    .data_stack = data_stack,
    .nursery_top = nursery_top,
    .nursery_start = nursery_start,
    .argc = argc,
  };
}
static void restore_regs(struct saved_regs regs) {
  self = regs.self;
  data_stack = regs.data_stack;
  nursery_top = regs.nursery_top;
  nursery_start = regs.nursery_start;
  argc = regs.argc;
}

static obj *apply(obj *func, obj *arg) {
  obj *blackhole_to_update = alloc(rt_blackhole_entry, 2);
  failwith("TODO");
}

// Write the normal form of 'self' (a value) to the buffer
static void quote(unsigned int lvl) {
  switch (GC_DATA(self)->tag) {
  case FUN:
  case PAP:
    {
      obj *var = alloc(rt_rigid_entry, 2);
      *INFO_WORD(var) = (struct info_word) { .size = 2, .var = lvl };
      *data_stack-- = var;
      argc = 1;
      failwith("TODO");
    }
  case RIGID:
    {
      failwith("TODO");
    }
  default:
    failwith("unreachable");
  }
}
