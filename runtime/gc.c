#include "gc.h"
#include "builtins.h"

enum gc_type { MAJOR, MINOR };
static void major_gc(void);
static obj *copy_to_old_space(obj *o, enum gc_type type);
static void process_copy_stack(enum gc_type type);
static void collect_roots(enum gc_type type);

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

static obj **data_stack_end;

void gc_init(void) {
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

  obj **data_stack_start = malloc(DATA_STACK_BYTES);
  data_stack = data_stack_end = data_stack_start + DATA_STACK_BYTES / sizeof(obj *);
}

void minor_gc(void) {
  // conservative heap check
  if ((size_t) old_top - (size_t) old_start < NURSERY_BYTES)
    return major_gc();

  DEBUG("Minor GC\n");

  collect_roots(MINOR);

  // Collect the remembered set
  obj **remembered_set_end = remembered_set + remembered_set_size;
  for (obj **o = remembered_set; o < remembered_set_end; o++) {
    // old_obj is 'REF ptr' where ptr points to the nursery
    obj *old_obj = *o;
    assert(old_obj->entrypoint == rt_ref_entry);
    old_obj->contents[0] =
      (word) copy_to_old_space((obj *) old_obj->contents[0], MINOR);
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

  DEBUG("copied %zu bytes\n", used_space);
}

static void collect_roots(enum gc_type type) {
  // Collect self
  self = copy_to_old_space(self, type);

  // Collect data stack
  for (obj **root = data_stack; root < data_stack_end; root++)
    *root = copy_to_old_space(*root, type);
}

static obj *copy_to_old_space(obj *o, enum gc_type type) {
  if (type == MINOR && !IS_YOUNG(o))
    return o;

  if (o->entrypoint == rt_forward_entry) {
    return (obj *) o->contents[0];
  } else if (o->entrypoint == rt_ref_entry) {
    // Compress REF indirections
    obj *new = copy_to_old_space((obj *) o->contents[0], type);
    o->contents[0] = (word) new;
    return new;
  } else {
    size_t size = GC_DATA(o)->size;
    if (!size) size = INFO_WORD(o)->size;
    obj *new = (obj *) (old_top -= size);
    memcpy(new, o, sizeof(word[size]));

    // set up forwarding
    o->entrypoint = rt_forward_entry;
    o->contents[0] = (word) new;

    // add to the copy stack
    if (copy_stack_size == copy_stack_cap) {
      size_t new_cap = 2 * copy_stack_cap + 1;
      copy_stack = reallocarray(copy_stack, new_cap, sizeof(obj *));
      copy_stack_cap = new_cap;
    }
    copy_stack[copy_stack_size++] = new;

    return new;
  }
}

static void process_copy_stack(enum gc_type type) {
  while (copy_stack_size > 0) {
    obj *o = copy_stack[--copy_stack_size];
    word *start;
    size_t size = GC_DATA(o)->size;
    if (size) {
      // Contains size - 1 many GC pointers
      start = &o->contents[0];
    } else {
      // Contains size - 2 many GC pointers
      size = INFO_WORD(o)->size;
      start = &o->contents[1];
    }
    word *end = &o->contents[size - 1];
    for (word *ptr = start; ptr < end; ptr++)
      *ptr = (word) copy_to_old_space((obj *) *ptr, type);
  }
}

// Write barrier: push thunk to the remembered set
void write_barrier(obj *thunk) {
  if (remembered_set_size == remembered_set_cap) {
    size_t new_cap = remembered_set_size * 2;
    remembered_set = reallocarray(remembered_set, new_cap, sizeof(obj *));
    remembered_set_cap = new_cap;
  }
  remembered_set[remembered_set_size++] = thunk;
}

