#include "t.h"

void runtime_init(void) {
  copy_stack = (obj **) malloc(4096);
  copy_stack_size = 0;
  copy_stack_cap = 4096 / sizeof(obj *);

  remembered_set = (obj **) malloc(4096);
  remembered_set_size = 0;
  remembered_set_cap = 4096 / sizeof(obj *);

  nursery_start = (word *) malloc(NURSERY_BYTES);
  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);

  /* old space will be allocated on first minor GC */
  old_size_bytes = 0;
  old_start = NULL;
  old_top = NULL;
  used_space_prev_gc = 0;

  data_stack_start = malloc(DATA_STACK_BYTES);
  data_stack_top = data_stack_start + DATA_STACK_BYTES / sizeof(obj *);
}

/* ought to be inlined */
obj *alloc(halfword size, halfword tag) {
  word *ptr = nursery_top - size - 1;
  if (ptr < nursery_start) {
    minor_gc();
    ptr = nursery_top - size - 1;
  }
  obj *o = (obj *) ptr;
  o->size = size;
  o->tag = tag;
  return o;
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
    assert(old_obj->tag == REF);
    copy_to_old_space((obj **) &old_obj->args[0], MINOR);
  }
  remembered_set_size = 0;

  process_copy_stack(MINOR);

  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);
}

void major_gc(void) {
  DEBUG("Major GC\n");

  word *from_space = old_start;

  size_t new_size = 2 * (used_space_prev_gc + NURSERY_BYTES);
  old_start = (word *) malloc(new_size);
  old_size_bytes = new_size; // is remembering the size necessary? eh
  old_top = (word *) ((char *) old_start + new_size);

  collect_roots(MAJOR);
  
  // ignore the remembered set
  remembered_set_size = 0;

  process_copy_stack(MAJOR);

  free(from_space);
  nursery_top = nursery_start + NURSERY_BYTES / sizeof(word);

  used_space_prev_gc = (size_t) old_top - (size_t) old_start;
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
  if (type == MINOR && !IS_YOUNG(x))
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

// Run self
void run(int argc) {
  assert(self->tag == THUNK || self->tag == CLOS);
  code *pc = (code *) self->args[0];
  obj *tmp = NULL;
# define VAR(off) \
    ((pc[off] == ARG ? data_stack_top : (obj **) self->args + 1)[pc[off+1]])
  for (;;) switch(*pc) {
  case ARGC_CHECK:
    {
      int params = pc[1];
      if (argc < params) {
        // Package it up in a partial application and return
        obj *pap = alloc(argc + 2, CLOS);
        pap->args[0] = (word) PAP_ENTRY_CODE;
        pap->args[1] = (word) self;
        memcpy(pap->args + 2, data_stack_top, argc * sizeof(obj *));
        data_stack_top += argc;
        upd(*data_stack_top++, pap);
        self = pap;
        return;
      }
      pc += 2;
      break;
    }
  case MKCLOS:
  case MKTHUNK:
    {
      int tag = *pc == MKCLOS ? CLOS : THUNK;
      int envc = pc[1];
      obj *o = alloc(envc + 1, tag);
      memcpy(&o->args[0], pc + 2, sizeof(word)); // memcpy since it's not properly aligned
      pc += 2 + sizeof(word);
      // add stuff to the env
      for (int i = 0; i < envc; i++)
        o->args[i + 1] = (word) VAR(2 * i);
      pc += 2 * envc;
      break;
    }
  case MORE_ARGS:
    {
      size_t n = pc[1];
      data_stack_top -= n;
#     ifndef NDEBUG
        memset(data_stack_top, 0, n * sizeof(obj *));
#     endif
      argc += n;
      pc += 2;
      break;
    }
  case FEWER_ARGS:
    {
      size_t n = pc[1];
      data_stack_top += n;
      argc -= n;
      pc += 2;
      break;
    }
  case MOV:
    {
      obj *o = VAR(1);
      data_stack_top[pc[3]] = o;
      pc += 4;
      break;
    }
  case READTMP:
    {
      int idx = pc[1];
      tmp = data_stack_top[idx];
      pc += 2;
      break;
    }
  case WRITETMP:
    {
      int idx = pc[1];
      data_stack_top[idx] = tmp;
      pc += 2;
      break;
    }
  case BH_SELF:
    {
      self->tag = BLACKHOLE;
      self->size = 1;
      pc++;
      break;
    }
  case CALL:
    {
      self = VAR(1);
      return force(argc);
    }
  case THIS_IS_A_PARTIAL_APPLICATION:
    {
      // In compiled code, this won't be compiled into closures, but rather a
      // built-in function in the runtime
      assert(self->args[0] == (word) PAP_ENTRY_CODE);

      // Add all the stuff as arguments and jump to the contained closure
      obj *clos = (obj *) self->args[1];
      int extra_args = self->size - 2;
      data_stack_top -= extra_args;
      memcpy(data_stack_top, self->args + 2, extra_args * sizeof(obj *));
      self = clos;
      return run(argc + extra_args);
    }
  default:
    failwith("unreachable");
  }
# undef VAR
}







