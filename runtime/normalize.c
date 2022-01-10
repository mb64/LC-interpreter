#include "gc.h"
#include "builtins.h"
#include "normalize.h"

static unsigned int *buf;
static size_t buf_len;
static size_t buf_cap;

static void push_buf(unsigned int x) {
  if (buf_len == buf_cap) {
    buf_cap *= 2;
    buf = reallocarray(buf, buf_cap, sizeof(unsigned int));
  }
  buf[buf_len++] = x;
}

// Pop an object from the data stack and write its normal form to the buffer
static void quote(void);

// Apply 'self' to an argument, returning the value in 'self'
static void apply(obj *arg);
// Evaluate 'self', returning the value in 'self'
static void eval(void);

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
  buf_len = 0;
  buf_cap = 16;
  buf = malloc(sizeof(unsigned int[buf_cap]));

  obj *main = alloc(entrypoint, 2);
  *INFO_WORD(main) = (struct info_word) { .size = 2, .var = 0 };
  self = main;

  quote();

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

// Apply 'self' to an argument, returning the value in 'self'
static void apply(obj *arg) {
  obj *blackhole_to_update = alloc(rt_blackhole_entry, 2);
  *INFO_WORD(blackhole_to_update) = (struct info_word) { .size = 2, .var = 0 };
  *--data_stack = blackhole_to_update;
  *--data_stack = arg;
  argc = 1;
  self->entrypoint();
  rt_update_thunk();
}
// Evaluate 'self', returning the value in 'self'
static void eval(void) {
  switch (GC_DATA(self)->tag) {
  case PAP:
  case RIGID:
  case FUN:
    return;
  case REF:
    self = (obj *) self->contents[0];
    return eval();
  case THUNK:
    obj *blackhole_to_update = alloc(rt_blackhole_entry, 2);
    *INFO_WORD(blackhole_to_update) = (struct info_word) { .size = 2, .var = 0 };
    *--data_stack = blackhole_to_update;
    argc = 0;
    self->entrypoint();
    rt_update_thunk();
    return;
  default:
    failwith("unreachable");
  }
}

// Write the normal form of 'self' to the buffer
static void quote(void) {
  // the next variable id to use for a lambda
  unsigned int next_var = 0;

  eval();

  // Use the data stack as a worklist
  obj **data_stack_end = data_stack;
  for (;;) {
    switch (GC_DATA(self)->tag) {
    case FUN:
    case PAP:
      {
        // Function f: λ x. quote (apply f x)
        unsigned int var_id = next_var++;
        push_buf(LAM);
        push_buf(var_id);
        obj *x = alloc(rt_rigid_entry, 2);
        *INFO_WORD(x) = (struct info_word) { .size = 2, .var = var_id };
        apply(x);
        continue;
      }
    case RIGID:
      {
        // Rigid term head args: head (map quote args)
        unsigned int argc = INFO_WORD(self)->size - 2;
        unsigned int var_id = INFO_WORD(self)->var;
        push_buf(NE);
        push_buf(argc);
        push_buf(var_id);
        data_stack -= argc;
        memcpy(data_stack, &self->contents[1], argc * sizeof(obj *));

        // Pop and evaluate the next item off the stack
        if (data_stack == data_stack_end)
          return;
        self = *data_stack++;
        eval();
        continue;
      }
    default:
      failwith("unreachable");
    }
  }
}


/***************** Printing ****************/

static unsigned int *print(unsigned int *nf, bool parens);
static unsigned int *print_rest_of_lam(unsigned int *nf);

void print_normal_form(unsigned int *nf) {
  print(nf, false);
  printf("\n");
}

static void print_var(unsigned int var) {
  // TODO: there's probably a nicer way to print variables
  if (var < 26)
    printf("%c", 'a' + var);
  else
    printf("v%u", var);
}

static unsigned int *print(unsigned int *nf, bool parens) {
  switch (*nf) {
  case LAM:
    if (parens) printf("(");
    printf("λ");
    nf = print_rest_of_lam(nf);
    if (parens) printf(")");
    return nf;
  case NE:
    nf++;
    unsigned int argc = *nf++;
    unsigned int var = *nf++;
    if (parens && argc) printf("(");
    print_var(var);
    for (unsigned i = 0; i < argc; i++) {
      printf(" ");
      nf = print(nf, true);
    }
    if (parens && argc) printf(")");
    return nf;
  default:
    failwith("unreachable");
  }
}

static unsigned int *print_rest_of_lam(unsigned int *nf) {
  switch (*nf) {
  case LAM:
    nf++;
    printf(" ");
    print_var(*nf++);
    return print_rest_of_lam(nf);
  case NE:
    printf(". ");
    return print(nf, false);
  default:
    failwith("unreachable");
  }
}


/************** Converting from church numerals ************/

size_t parse_church_numeral(unsigned int *nf) {
# define CONSUME(x) if (*nf++ != x) failwith("Not a church numeral")
  CONSUME(LAM);
  unsigned s = *nf++;
  CONSUME(LAM);
  unsigned z = *nf++;
  size_t n = 0;
  for(;;) {
    CONSUME(NE);
    unsigned argc = *nf++;
    if (argc == 0) {
      CONSUME(z);
      return n;
    } else if (argc == 1) {
      CONSUME(s);
      n++;
      continue;
    } else {
      failwith("Not a church numeral");
    }
  }
}
