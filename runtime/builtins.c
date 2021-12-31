#include "gc.h"
#include "builtins.h"

void rt_gc(void) {
  minor_gc();
}

void rt_too_few_args(void) {
  if (argc == 0)
    return;
  size_t size = argc + 3;
  obj *pap = alloc(rt_pap_entry, size);
  *INFO_WORD(pap) = (struct info_word) { .size = size, .var = 0 };
  pap->contents[1] = (word) self;
  memcpy(&pap->contents[2], data_stack, argc * sizeof(obj *));
  data_stack += argc;
  argc = 0;
  self = pap;
}

void rt_update_thunk(void) {
  obj *thunk = *data_stack++;
  thunk->entrypoint = rt_ref_entry;
  thunk->contents[0] = (word) self;
  if (!IS_YOUNG(thunk) && IS_YOUNG(self))
    write_barrier(thunk);
}

void rt_adjacent_update_frames(void) {
  obj *thunk = *data_stack;
  thunk->entrypoint = rt_ref_entry;
  thunk->contents[0] = (word) self;
  if (!IS_YOUNG(thunk) && IS_YOUNG(self))
    write_barrier(thunk);
  *data_stack = self;
}

/************** Built-in heap objects *************/

// FIXME: they need to have info tables before the executable code

void rt_ref_entry(void) {
  self = (obj *) self->contents[0];
  // Hope for a tail call :/
  return self->entrypoint();
}

void rt_forward_entry(void) {
  failwith("unreachable: forward objects only exist during GC\n");
}

void rt_pap_entry(void) {
  // Partial application: push the arguments onto the stack and tail call the
  // contained function
  obj *fun = (obj *) self->contents[1];
  size_t extra_args = INFO_WORD(self)->size - 3;
  argc += extra_args;
  data_stack -= extra_args;
  memcpy(data_stack, &self->contents[2], extra_args);
  self = fun;
  // Hope for a tail call :/
  return self->entrypoint();
}

void rt_rigid_entry(void) {
  // Rigid term: allocate a new rigid term with the new arguments
  if (argc == 0)
    return;

  obj *new = alloc(rt_rigid_entry, INFO_WORD(self)->size + argc);
  *INFO_WORD(new) = (struct info_word) {
    .size = INFO_WORD(self)->size,
    .var = INFO_WORD(self)->var,
  };

  size_t self_argc = INFO_WORD(self)->size - 2;
  memcpy(&new->contents[1], &self->contents[1], sizeof(word) * self_argc);
  memcpy(&new->contents[1 + self_argc], data_stack, sizeof(word) * argc);
  data_stack += argc;
  argc = 0;

  self = new;
}

void rt_blackhole_entry(void) {
  failwith("Black hole (infinite loop?)\n");
}
