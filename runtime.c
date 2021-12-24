#include "runtime.h"

enum gc_type { MAJOR, MINOR };
static void copy_to_old_space(obj **o, enum gc_type type);
static void process_copy_stack(enum gc_type type);
static void collect_roots(enum gc_type type);

// Simple generational semispace GC
// Allocations go downards
#define NURSERY_BYTES (2*1024*1024) // 2M nursery
static word *nursery_start;
static word *nursery_top;
#define IS_YOUNG(o) ((size_t) (o) - (size_t) nursery_start < NURSERY_BYTES)

static word *old_start;
static word *old_top;
static word *other_old_start;
static size_t old_space_size;
static size_t other_old_space_size;

// Remembered set: a growable (malloc'd) vector of old objects 'REF ptr' that
// point to the nursery
static obj **remembered_set;
static size_t remembered_set_size;
static size_t remembered_set_cap;

// Copy stack: during GC, a worklist of new to-space objects whose fields still
// point to the from-space
static obj **copy_stack;
static size_t copy_stack_size;
static size_t copy_stack_cap;

obj **data_stack_top;
obj **data_stack_start;

obj *self;

void runtime_init(void) {
  copy_stack = (obj **) malloc(4096);
  copy_stack_size = 0;
  copy_stack_cap = 4096 / sizeof(obj *);

  remembered_set = (obj **) malloc(4096);
  remembered_set_size = 0;
  remembered_set_cap = 4096 / sizeof(obj *);

  nursery_start = (word *) malloc(NURSERY_BYTES);
  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);

  old_start = (word *) malloc(2 * NURSERY_BYTES);
  old_top = old_start + 2 * NURSERY_BYTES / sizeof(word);
  other_old_start = NULL;
  old_space_size = other_old_space_size = 2 * NURSERY_BYTES;

  data_stack_start = malloc(DATA_STACK_BYTES);
  data_stack_top = data_stack_start + DATA_STACK_BYTES / sizeof(obj *);
}

/* ought to be inlined */
obj *alloc(halfword size, halfword tag) {
  /* DEBUG("Allocating 1+%d words\n", size); */
  word *ptr = nursery_top - size - 1;
  if (ptr < nursery_start) {
    minor_gc();
    ptr = nursery_top - size - 1;
  }
  nursery_top = ptr;
  obj *o = (obj *) ptr;
  o->size = size;
  o->tag = tag;
  return o;
}

void minor_gc(void) {
  // conservative heap check
  if ((size_t) old_top - (size_t) old_start < NURSERY_BYTES)
    return major_gc();

  /* DEBUG("Minor GC\n"); */

  collect_roots(MINOR);

  // Collect the remembered set
  obj **remembered_set_end = remembered_set + remembered_set_size;
  for (obj **o = remembered_set; o < remembered_set_end; o++) {
    // old_obj is 'REF ptr' where ptr points to the nursery
    obj *old_obj = *o;
    assert(old_obj->tag == REF);
    copy_to_old_space((obj **) &old_obj->args[0], MINOR);
  }
  remembered_set_size = 0;

  process_copy_stack(MINOR);

  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);
}

void major_gc(void) {
  DEBUG("Major GC: ");

  if (!other_old_start)
    other_old_start = (word *) malloc(other_old_space_size);
  word *from_space = old_start;
  size_t from_space_size = old_space_size;
  old_start = other_old_start;
  old_space_size = other_old_space_size;
  old_top = old_start + old_space_size / sizeof(word);
  other_old_start = NULL;

  collect_roots(MAJOR);
  process_copy_stack(MAJOR);

  // set a new size for the old space if needed
  size_t used_space = (size_t) old_start + old_space_size - (size_t) old_top;
  if (used_space + NURSERY_BYTES > old_space_size)
    other_old_space_size *= 2;

  if (from_space_size < other_old_space_size)
    free(from_space);
  else
    other_old_start = from_space;

  // reset the nursery and ignore the remembered set
  remembered_set_size = 0;
  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);

  DEBUG("copied %llu bytes\n", used_space);
}

static void collect_roots(enum gc_type type) {
  // Collect self
  copy_to_old_space(&self, type);

  // Collect data stack
  obj **data_stack_end = data_stack_start + DATA_STACK_BYTES / sizeof(obj *);
  for (obj **root = data_stack_top; root < data_stack_end; root++)
    copy_to_old_space(root, type);
}

static void copy_to_old_space(obj **x, enum gc_type type) {
  obj *o = *x;
  if (type == MINOR && !IS_YOUNG(o))
    return;

  if (o->tag == FORWARD) {
    *x = (obj *) o->args[0];
  } else if (o->tag == REF) {
    // Compress REF indirections
    copy_to_old_space((obj **) &o->args[0], type);
    *x = (obj *) o->args[0];
  } else {
    obj *new = (obj *) (old_top -= 1 + o->size);
    memcpy(new, o, (1 + o->size) * sizeof(word));

    // set up forwarding
    o->tag = FORWARD;
    o->args[0] = (word) new;

    // add to the copy stack
    if (copy_stack_size == copy_stack_cap) {
      size_t new_cap = 2 * copy_stack_cap + 1;
      copy_stack = reallocarray(copy_stack, new_cap, sizeof(obj *));
      copy_stack_cap = new_cap;
    }
    copy_stack[copy_stack_size++] = new;

    *x = new;
  }
}

static void process_copy_stack(enum gc_type type) {
  while (copy_stack_size > 0) {
    obj *o = copy_stack[--copy_stack_size];
    assert(o->tag == THUNK || o->tag == CLOS || o->tag == RIGID);
    // They all have the same layout: header [not gc ptr] [gc ptr]...
    word *end = &o->args[o->size];
    for (word *ptr = &o->args[1]; ptr < end; ptr++)
      copy_to_old_space((obj **) ptr, type);
  }
}

// The top of the data stack holds a thunk to update with this value. Do that.
// (Includes write barrier)
void upd(obj *thunk, obj *val) {
  thunk->tag = REF;
  thunk->size = 1;
  thunk->args[0] = (word) val;

  // Write barrier: push val to the remembered set
  if (!IS_YOUNG(thunk) && IS_YOUNG(val)) {
    if (remembered_set_size == remembered_set_cap) {
      size_t new_cap = remembered_set_size * 2;
      remembered_set = reallocarray(remembered_set, new_cap, sizeof(obj *));
      remembered_set_cap = new_cap;
    }
    remembered_set[remembered_set_size++] = thunk;
  }
}

void force(int argc) {
  while (self->tag == REF)
    self = (obj *) self->args[0];

  if (argc == 0) {
    // No arguments to apply, need to update the thunk
    upd(*data_stack_top++, self);

    if (self->tag == THUNK) {
      // Updating the thunk before forcing self allows this to be a tail call
      *--data_stack_top = self;
      return run(0);
    }
    return;
  }

  if (self->tag == THUNK) {
    *--data_stack_top = self;
    run(0);
  }

  switch (self->tag) {
  case CLOS:
    return run(argc);
  case RIGID:
    obj *value = alloc(self->size + argc, RIGID);
    // copy both the head and the first n args
    memcpy(value->args, self->args, self->size * sizeof(word));
    // copy the new n args
    memcpy(value->args + self->size, data_stack_top, argc * sizeof(word));
    // pop all the arguments
    data_stack_top += argc;
    // update and return!
    upd(*data_stack_top++, value);
    self = value;
    return;
  default: failwith("unreachable");
  }
}



