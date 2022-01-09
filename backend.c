#include "backend.h"
#include "runtime/data_layout.h"
#include "runtime/builtins.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>
#include <sys/mman.h>
#include <errno.h>


/************** General utils *************/

#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

static void init_code_buf(void);

static uint8_t *code_buf_start = NULL;
static uint8_t *code_buf = NULL;
static uint8_t *code_buf_end = NULL;

static void write_header(uint32_t size, uint32_t tag);
static void write_code(size_t len, const uint8_t code[len]);

#define CODE(...) do { \
    const uint8_t code[] = { __VA_ARGS__ }; \
    write_code(sizeof(code), code); \
  } while (0)
#define U32(x) \
  (uint8_t)  (x)       , (uint8_t) ((x) >> 8), \
  (uint8_t) ((x) >> 16), (uint8_t) ((x) >> 24)
#define U64(x) \
  (uint8_t)  (x)       , (uint8_t) ((x) >> 8),  \
  (uint8_t) ((x) >> 16), (uint8_t) ((x) >> 24), \
  (uint8_t) ((x) >> 32), (uint8_t) ((x) >> 40), \
  (uint8_t) ((x) >> 48), (uint8_t) ((x) >> 56)

enum reg {
  RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI,
  R8,  R9,  R10, R11, R12, R13, R14, R15
};
#define SELF        RBX
#define DATA_STACK  R12
#define HEAP_PTR    R13
#define HEAP_LIMIT  R14
#define ARGC        R15

static void mem64(uint8_t opcode, enum reg reg, enum reg ptr, int32_t offset);
static void reg64(uint8_t opcode, enum reg reg, enum reg other_reg);

#define OP_LOAD 0x8b
#define OP_STORE 0x89

#define LOAD(reg, ptr, offset) \
  mem64(OP_LOAD, reg, ptr, offset)
#define STORE(reg, ptr, offset) \
  mem64(OP_STORE, reg, ptr, offset)
#define MOV_RR(dest, src) reg64(OP_STORE, src, dest)

// Add constant value 'imm' to register 'reg'
static void add_imm(enum reg reg, int32_t imm);

// idx 0 is the first env item. env is usually SELF
static void load_env_item(enum reg reg, enum reg env, size_t idx);
// idx 0 is the top of the stack
static void load_arg(enum reg reg, size_t idx);
static void store_arg(size_t idx, enum reg reg);

static void blackhole_self(void);


struct var_info {
  bool is_used;
  // Only when used
  size_t env_idx;
};

struct env {
  // do we need?
  struct env *up;
  var args_start;
  size_t envc;
  // args_start elements, envc of which are used
  struct var_info upvals[];
};

struct compile_result {
  void *code;
  struct env *env;
};

static void make_sure_can_access_var(struct env *env, var v);


static void init_code_buf(void) {
  if (code_buf)
    return;

  // Need to mmap it so that I can mprotect it later.
  size_t len = 8 * 1024 * 1024;
  code_buf = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (!code_buf)
    failwith("Couldn't allocate buffer for code\n");
  code_buf_start = code_buf;
  code_buf_end = code_buf_start + len;
}

void compile_finalize(void) {
  // mprotect it
  size_t len = code_buf_end - code_buf_start;
  if (mprotect(code_buf_start, len, PROT_READ | PROT_EXEC))
    failwith("Couldn't map as executable: %s\n", strerror(errno));
}

static void write_code(size_t len, const uint8_t code[len]) {
  uint8_t *end = code_buf + len;
  if (end > code_buf_end) failwith("Too much code");
  memcpy(code_buf, code, len);
  code_buf = end;
}

#define REXW(R,X,B) \
  (0x48 | ((R >> 1) & 4) | ((X >> 2) & 3) | (B >> 3))
#define MODRM(Mod, Reg, RM) \
  (((Mod) << 6) | ((Reg & 7) << 3) | (RM & 7))

static void reg64(uint8_t opcode, enum reg reg, enum reg other_reg) {
  // Mod == 11: r/m
  CODE(
    REXW(reg, 0, other_reg),
    opcode,
    MODRM(3, reg, other_reg)
  );
}

static void mem64(uint8_t opcode, enum reg reg, enum reg ptr, int32_t offset) {
  CODE(REXW(reg, 0, ptr), opcode);

  if ((ptr & 7) == RSP) {
    // r/m == rsp: [SIB]

    if (offset == 0)
      // Mod == 00 && index == rsp: [base]
      CODE(MODRM(0, reg, RSP), 0x24);
    else if (-128 <= offset && offset < 128)
      // Mod == 01 && index == rsp: [base + disp8]
      CODE(MODRM(1, reg, RSP), 0x24, (uint8_t) offset);
    else
      // Mod == 10 && index == rsp: [base + disp32]
      CODE(MODRM(2, reg, RSP), 0x24, (uint32_t) offset);

    return;
  }

  if (offset == 0 && (ptr & 7) != RBP)
    // Mod == 00: [r/m]
    CODE(MODRM(0, reg, ptr));
  else if (-128 <= offset && offset < 128)
    // Mod == 01: [r/m + disp8]
    CODE(MODRM(1, reg, ptr), (uint8_t) offset);
  else
    // Mod == 10: [r/m + disp32]
    CODE(MODRM(2, reg, ptr), U32((uint32_t) offset));
}

static void add_imm(enum reg reg, int32_t imm) {
  if (-128 <= imm && imm < 128)
    CODE(REXW(0, 0, reg), 0x83, MODRM(3, 0, reg), (uint8_t) imm);
  else
    CODE(REXW(0, 0, reg), 0x81, MODRM(3, 0, reg), U32((uint32_t) imm));
}

static void make_sure_can_access_var(struct env *env, var v) {
  while (v < env->args_start && !env->upvals[v].is_used) {
    env->upvals[v].is_used = true;
    env->upvals[v].env_idx = env->envc++;
    env = env->up;
  }
}

static void load_env_item(enum reg reg, enum reg env, size_t idx) {
  assert(idx < INT_MAX / 8 - 8);
  LOAD(reg, env, 8 * idx + 8);
}
static void load_arg(enum reg reg, size_t idx) {
  assert(idx < INT_MAX / 8);
  LOAD(reg, DATA_STACK, 8 * idx);
}
static void store_arg(size_t idx, enum reg reg) {
  assert(idx < INT_MAX / 8);
  STORE(reg, DATA_STACK, 8 * idx);
}

static void blackhole_self(void) {
  // Use rax as a temporary register since it's possible that both rsi and rdi
  // are in use
  // movabs rax, rt_blackhole_entry
  CODE(0x48, 0xb8, U64((uint64_t) rt_blackhole_entry));
  STORE(RAX, SELF, 0);
  CODE(0xb8, U32(2)); // mov esi, 2
  STORE(RAX, SELF, 8);
}


/******************* Prologue *****************/

static void *start_closure(size_t argc, size_t envc);
static void *start_thunk(size_t envc);


static void write_header(uint32_t size, uint32_t tag) {
  // Align up to nearest word
  code_buf = (uint8_t *) (((size_t) code_buf + 7) & ~7);

  if (code_buf + 8 > code_buf_end) failwith("Too much code");

  memcpy(code_buf, &size, sizeof(uint32_t));
  code_buf += sizeof(uint32_t);
  memcpy(code_buf, &tag, sizeof(uint32_t));
  code_buf += sizeof(uint32_t);
}

static void *start_closure(size_t argc, size_t envc) {
  /* assert(argc < INT_MAX); */
  assert(argc < 127); // TODO: allow more args (? maybe)
  assert(envc < INT_MAX);

  write_header(envc == 0 ? 0 : envc + 1, FUN);
  void *code_start = code_buf;

  CODE(
    // cmp r15, argc
    0x49, 0x83, 0xff, (uint8_t) argc,
    // jge rest_of_code (+12)
    0x7d, 12,
    // movabs rt_too_few_args, %rdi
    0x48, 0xbf, U64((size_t) rt_too_few_args),
    // jmp *%rdi
    0xff, 0xe7
    // rest_of_code:
  );

  return code_start;
}

static void *start_thunk(size_t envc) {
  assert(envc < INT_MAX);

  write_header(envc == 0 ? 0 : envc + 1, FUN);
  void *code_start = code_buf;

  CODE(
    // test argc,argc (argc is %r15)
    0x4d, 0x85, 0xff,
    // jz adjacent_updates (+31)
    0x74, 31,
    // sub data_stack, $8 (data_stack is %r12)
    0x49, 0x83, 0xec, 0x08,
    // mov [data_stack], self (self is rbx)
    0x49, 0x89, 0x1c, 0x24,
    // push argc (argc is r15)
    0x41, 0x57,
    // call rest_of_code (+28)
    0xe8, U32(28),
    // pop argc (argc is %r15)
    0x41, 0x5f,
    // movabs rdi, rt_update_thunk
    0x48, 0xbf, U64((size_t) rt_update_thunk),
    // call rdi
    0xff, 0xd7,
    // jmp [qword ptr [self]] (self is rbx)
    0xff, 0x23,
    // adjacent_updates:
    // movabs rdi, rt_avoid_adjacent_update_frames
    0x48, 0xbf, U64((size_t) rt_adjacent_update_frames),
    // call rdi
    0xff, 0xd7
    // rest_of_code:
  );

  return code_start;
}


/***************** Allocations ****************/

static void do_allocations(size_t lvl, struct env *this_env, size_t n, struct compile_result locals[n]);


static void heap_check(size_t bytes_allocated) {
  // TODO: better maximum allocation size control
  assert(bytes_allocated < 131072);

  add_imm(HEAP_PTR, - (int32_t) bytes_allocated);
  CODE(
    // cmp heap, heap limit (r13,r14)
    0x4d, 0x39, 0xf5,
    // jae alloc_was_good (offset depends on imm8 vs imm32)
    // TODO: add assertions that this is correct
    0x73, (bytes_allocated <= 128 ? 16 : 19),
    // movabs rdi, rt_gc
    0x48, 0xbf, U64((size_t) rt_gc),
    // call rdi
    0xff, 0xd7
  );
  add_imm(HEAP_PTR, - (int32_t) bytes_allocated);
  // alloc_was_good:
}

static void load_var(size_t lvl, struct env *this_env, enum reg dest, var v) {
  assert(v < lvl);
  if (v >= this_env->args_start) {
    load_arg(dest, lvl - v - 1);
  } else {
    assert(this_env->upvals[v].is_used);
    load_env_item(dest, SELF, this_env->upvals[v].env_idx);
  }
}

static void do_allocations(size_t lvl, struct env *this_env, size_t n, struct compile_result locals[n]) {
  if (n == 0)
    return;

  size_t words_allocated = 0;
  for (size_t i = 0; i < n; i++) {
    if (locals[i].env->envc == 0)
      words_allocated += 2;
    else
      words_allocated += locals[i].env->envc + 1;
  }

  heap_check(8 * words_allocated);

  MOV_RR(RDI, HEAP_PTR);
  for (size_t i = 0; i < n; i++) {
    lvl++;
    add_imm(DATA_STACK, -8);
    STORE(RDI, DATA_STACK, 0);

    // Store the entrypoint
    // movabs rsi, entrypoint
    CODE(0x48, 0xbe, U64((uint64_t) locals[i].code));
    STORE(RSI, RDI, 0);

    // Store the contents
    struct env *env = locals[i].env;
    assert(env->up == this_env);
    size_t count = 0;
    for (var v = 0; v < env->args_start; v++) {
      if (!env->upvals[v].is_used)
        continue;
      count++;
      load_var(lvl, this_env, RSI, v);
      size_t offset = 8 + 8*env->upvals[v].env_idx;
      STORE(RSI, RDI, offset);
    }
    assert(count == env->envc);

    if (env->envc == 0) {
      // Store the info_word
      CODE(0xbe, U32(2)); // mov esi, 2
      STORE(RSI, RDI, 8);
    }

    // Bump rdi, used as a temporary heap pointer
    if (i != n-1)
      add_imm(RDI, env->envc == 0 ? 16 : 8 + 8*env->envc);
  }
}


/***************** Shuffle arguments ****************/

static void do_the_moves(size_t lvl, ir term, struct env *env);


enum mov_status { NOT_STARTED, IN_PROGRESS, DONE };

struct dest_info_item {
  enum { FROM_ARGS, FROM_ENV } src_type;
  int src_idx;
  int next_with_same_src; // -1 if none
  enum mov_status status;
};

typedef struct {
  size_t n;
  struct dest_info_item *dest_info; // n + 1 of them
  int *src_to_dest; // n + 1 of them
  int in_rdi;
  bool for_a_thunk;
} mov_state;

// Store src to all its destinations, so that it can be overwritten afterwards.
static void vacate_one(mov_state *s, int src) {
  printf("Vacating %d\n"); // FIXME

  switch (s->dest_info[src].status) {
  case DONE:
    printf("It was done\n"); // FIXME
    break;

  case IN_PROGRESS:
    printf("A Cycle!\n"); // FIXME
    // A cycle! Use rdi as a temporary register to break the cycle
    assert(s->in_rdi == -1);
    s->in_rdi = src;
    if (src == s->n)
      MOV_RR(RDI, SELF);
    else
      load_arg(RDI, src);
    break;

  case NOT_STARTED:
    printf("Not started\n"); // FIXME
    if (s->src_to_dest[src] == -1) {
      printf("Didn't do anything\n"); // FIXME
      // Don't do anything if it has no destinations
      s->dest_info[src].status = DONE;
      break;
    }

#   define FOREACH_DEST(dest) \
      for (int dest = s->src_to_dest[src]; dest != -1; dest = s->dest_info[dest].next_with_same_src)

    s->dest_info[src].status = IN_PROGRESS;
    if (src == s->n) {
      // Clear out 'self' by storing all the things from the env
      FOREACH_DEST(dest) {
        assert(s->dest_info[dest].src_type == FROM_ENV);

        vacate_one(s, dest);

        enum reg self = s->in_rdi == s->n ? RDI : SELF;
        if (dest == s->n) {
          if (s->for_a_thunk) {
            load_env_item(RSI, self, s->dest_info[dest].src_idx);
            blackhole_self();
            MOV_RR(SELF, RSI);
          } else {
            load_env_item(SELF, self, s->dest_info[dest].src_idx);
          }
        } else {
          load_env_item(RSI, self, s->dest_info[dest].src_idx);
          store_arg(dest, RSI);
        }
      }
    } else {
      // Clear out data_stack[src]
      FOREACH_DEST(dest) {
        assert(s->dest_info[dest].src_type == FROM_ARGS);
        assert(s->dest_info[dest].src_idx == src);
        vacate_one(s, dest);
      }
      enum reg src_reg;
      if (s->in_rdi == src) {
        src_reg = RDI;
      } else {
        load_arg(RSI, src);
        src_reg = RSI;
      }
      FOREACH_DEST(dest) {
        if (dest == s->n) {
          if (s->for_a_thunk)
            blackhole_self();
          MOV_RR(SELF, src_reg);
        } else {
          store_arg(dest, src_reg);
        }
      }
    }
    if (s->in_rdi == src)
      s->in_rdi = -1;
    s->dest_info[src].status = DONE;
    printf("Done with %d!\n", src); // FIXME
#   undef FOREACH_DEST
    break;
  }
}

static void add_dest_to_mov_state(size_t lvl, struct env *env, mov_state *s, int dest, var v) {
  assert(v <= lvl);
  if (v >= env->args_start) {
    // It's from the data stack
    int src = lvl - v - 1;
    s->dest_info[dest] = (struct dest_info_item) {
      .src_type = FROM_ARGS,
      .src_idx = src,
      .next_with_same_src = s->src_to_dest[src],
      .status = NOT_STARTED
    };
    if (dest != src)
      s->src_to_dest[src] = dest;
  } else {
    // It's from the env
    assert(env->upvals[v].is_used);
    s->dest_info[dest] = (struct dest_info_item) {
      .src_type = FROM_ENV,
      .src_idx = env->upvals[v].env_idx,
      .next_with_same_src = s->src_to_dest[s->n],
      .status = NOT_STARTED
    };
    s->src_to_dest[s->n] = dest;
  }
}

static void do_the_moves(size_t lvl, ir term, struct env *env) {
  //FIXME
  printf("Moving for ");
  print_ir(term);

  assert(lvl == term->lvl + term->arity + term->lets_len);
  assert(term->lvl == env->args_start);

  size_t incoming_argc = term->arity + term->lets_len;
  size_t outgoing_argc = 0;
  for (arglist arg = term->args; arg; arg = arg->prev)
    ++outgoing_argc;
  printf("Outgoing argc is %d, incoming %d\n", outgoing_argc, incoming_argc);


  // Resize the data stack
  size_t n;
  if (outgoing_argc > incoming_argc) {
    n = outgoing_argc;
    size_t diff = outgoing_argc - incoming_argc;
    assert(diff < INT_MAX / 8);
    lvl += diff;
    add_imm(DATA_STACK, -8 * (int) diff);
  } else {
    n = incoming_argc;
  }

  // Generate the data structures and stuff
  mov_state s = (mov_state) {
    .n = n,
    .dest_info = malloc(sizeof(struct dest_info_item[n+1])),
    .src_to_dest = malloc(sizeof(int[n+1])),
    .in_rdi = -1,
    .for_a_thunk = term->arity == 0,
  };

  for (int i = 0; i < n + 1; i++)
    s.src_to_dest[i] = -1;

  int dest_start =
    outgoing_argc < incoming_argc ? incoming_argc - outgoing_argc : 0;
  int dest = 0;
  for (; dest < dest_start; dest++)
    s.dest_info[dest].status = NOT_STARTED;
  for (arglist arg = term->args; arg; dest++, arg = arg->prev)
    add_dest_to_mov_state(lvl, env, &s, dest, arg->arg);
  assert(dest == n);
  add_dest_to_mov_state(lvl, env, &s, n, term->head);

  // FIXME
  for (int i = 0; i < n + 1; i++)
    printf("%d->%d ", i, s.src_to_dest[i]);
  printf("\n");

  // Do all the moving
  for (int i = 0; i < n + 1; i++) {
    vacate_one(&s, i);
    assert(s.in_rdi == -1);
  }

  // Resize the data stack and set argc
  if (outgoing_argc < incoming_argc) {
    size_t diff = incoming_argc - outgoing_argc;
    assert(diff < INT_MAX / 8);
    add_imm(DATA_STACK, 8 * (int) diff);
  }
  add_imm(ARGC, outgoing_argc - term->arity);
}


/***************** Perform the call! ****************/

static void call_self(void) {
  // jmp [qword ptr [self]]  (self is rbx)
  CODE(0xff, 0x23);
}


/***************** Tying it all together ****************/

void *compile_toplevel(ir term);


static struct compile_result compile(struct env *up, ir term) {
  size_t lvl = term->lvl;

  // Allocate an environment
  struct env *env = malloc(sizeof(struct env) + sizeof(struct var_info[lvl]));
  env->up = up;
  env->args_start = lvl;
  env->envc = 0;
  for (int i = 0; i < lvl; i++)
    env->upvals[i].is_used = false;

  // Populate the env
  make_sure_can_access_var(env, term->head);
  for (arglist arg = term->args; arg; arg = arg->prev)
    make_sure_can_access_var(env, arg->arg);

  // Compile all the lets
  struct compile_result *locals =
    malloc(sizeof(struct compile_result[term->lets_len]));
  int i = 0;
  for (letlist let = term->lets; let; let = let->next, i++)
    locals[i] = compile(env, let->val);
  assert(i == term->lets_len);

  // Prologue
  void *code_start;
  if (term->arity == 0)
    code_start = start_thunk(env->envc);
  else
    code_start = start_closure(term->arity, env->envc);

  lvl += term->arity;

  // Allocations
  do_allocations(lvl, env, term->lets_len, locals);
  for (int i = 0; i < term->lets_len; i++)
    free(locals[i].env);
  free(locals);

  lvl += term->lets_len;

  // Set up for call
  do_the_moves(lvl, term, env);

  // Execute the call!
  call_self();

  return (struct compile_result) {
    .code = code_start,
    .env = env,
  };
}

void *compile_toplevel(ir term) {
  init_code_buf();
  assert(term->lvl == 0);
  struct compile_result res = compile(NULL, term);
  assert(res.env->envc == 0);
  free(res.env);
  return res.code;
}


// TODO:
//
//  - [X] Implement mmap and mprotect stuff
//  - [X] Fix lowering to IR -- it does wrong things
//  - [X] Fix the thunk entry code
//  - [X] Implement parallel move for shuffling args
//  - [X] Tie it all together in a big compile function
//  - [X] *Really* tie everything together with a main function!
//  - [ ] Fix all the (I'm sure many) mistakes
//







