#include "frontend.h"
#include "runtime/data_layout.h"
#include "runtime/builtins.h"

/************** IR **************/

static word *ir_arena = NULL;
#define IR_ARENA_SIZE (32 * 1024 * 1024)

static void arena_init(void) {
  if (!ir_arena)
    ir_arena = (word *) malloc(IR_ARENA_SIZE);
}

#define ARENA_ALLOC(ty, ...) \
  ty *node = (ty *) ir_arena; \
  ir_arena += sizeof(ty) / sizeof(*ir_arena); \
  *node = (ty) { __VA_ARGS__ }; \
  return node;

static arglist snoc_arg(arglist prev, var arg) {
  ARENA_ALLOC(struct an_arg, prev, arg)
}
static letlist cons_let(ir val, letlist next) {
  ARENA_ALLOC(struct a_let, val, next)
}

static ir mkvar(size_t lvl, var v) {
  ARENA_ALLOC(struct exp, 0, NULL, NULL, 0, v, NULL);
}
#undef ARENA_ALLOC

static bool is_var(ir e) {
  return e->arity == 0 && e->lets == NULL && e->args == NULL;
}
static bool is_lambda(ir e) {
  return e->arity > 0;
}

static ir mkabs(size_t lvl, ir body) {
  body->arity++;
  return body;
}

static ir mkapp(size_t lvl, ir func, ir arg) {
  if (is_lambda(func)) {
    // Applying a lambda: (λx. body) arg  ⇒  let x = arg in body
    func->arity--;
    func->lets = cons_let(arg, func->lets);
    func->lets_len++;
    if (!func->lets_end)
      func->lets_end = &func->lets->next;
    return func;
  } else if (is_var(arg)) {
    // Applying a thunk to a var:
    //  (let ... in f args) x  ⇒  let ... in f args x
    var v = arg->head;
    func->args = snoc_arg(func->args, v);
    return func;
  } else {
    // Appying a thunk to something complex:
    //  (let ... in f args) arg ⇒ let ... in let x = arg in f args x
    var new_var = lvl + func->lets_len;

    // add the let
    letlist new_let = cons_let(arg, NULL);
    if (func->lets_end)
      *func->lets_end = new_let;
    else
      func->lets = new_let;
    func->lets_end = &new_let->next;
    func->lets_len++;

    // add the arg
    func->args = snoc_arg(func->args, new_var);

    return func;
  }
}

/*************** Parser ***************/

static const char *err_msg = NULL;
static const char *err_loc = NULL;

static ir parse(char *text) {
  char *cursor = text;
  skip_whitespace(&cursor);
  ir result = parse_exp(&cursor, 0, NULL);
  if (result && *cursor != '\0') {
    result = NULL;
    err_loc = text;
    err_msg = "expected eof";
  }

  if (!result)
    // TODO: better error message printing
    printf("parse error at byte %d :/\n%s\n", err_loc - text, err_msg);

  return result;
}

static bool skip_whitespace(char **cursor) {
  char *text = *cursor;
  // comments use /- -/
  for (;;) switch (text[0]) {
    case ' ':
    case '\n':
    case '\t':
      text++;
      continue;
    case '/':
      if (text[1] == '-') {
        // comment start!
        text += 2;
        while (text[0]) {
          if (text[0] == '-' && text[1] == '/') {
            text += 2;
            continue;
          }
          text++;
        }
        // text[0] == '\0'
        err_loc = *cursor = text;
        err_msg = "reached EOF during comment";
        return false;
      } else { /* fallthrough */ }
    default:
      *cursor = text;
      return true;
  }
}

// returns the length of the ident starting at *cursor
static size_t parse_ident(char **cursor) {
  // parse [a-zA-Z_]+
  char *start = *cursor;
  char *end = start;
#define IDENT_CHAR(c) ('A' <= c && c <= 'Z' || 'a' <= c && c <= 'z' || c == '_')
  while (IDENT_CHAR(*end))
    end++;
  *cursor = end;
  if (start == end) {
    err_loc = start;
    err_msg = "expected variable";
    return 0;
  }
  SKIP_WHITESPACE(cursor);
  return end - start;
}

static ir parse_var(char **cursor, size_t lvl, scope s) {
  char *start = *cursor;
  size_t len = parse_ident(cursor);
  if (!len) return NULL;

  // resolve the name
  var v = lvl;
  scope node = s;
  while (node) {
    v--;
    if (len == node->name_len && strncmp(start, node->name, len) == 0)
      return mkvar(lvl, v);
    else
      node = node->next;
  }
  err_loc = start;
  err_msg = "variable not in scope";
  return NULL;
}

// atomic_exp ::= var | '(' exp ')'
static ir parse_atomic_exp(char **cursor, size_t lvl, scope s) {
  if (**cursor == '(') {
    ++*cursor;
    SKIP_WHITESPACE(cursor);
    ir result = parse_exp(cursor, lvl, s);
    if (**cursor == ')') {
      ++*cursor;
      SKIP_WHITESPACE(cursor);
      return result;
    } else {
      err_loc = *cursor;
      err_msg = "expected ')'";
      return NULL;
    }
  }
  return parse_var(cursor, lvl, s);
}

// args ::= atomic_exp*
static ir then_parse_some_args(char **cursor, size_t lvl, scope s, ir func) {
  while (**cursor != ')') {
    ir arg = parse_atomic_exp(cursor, lvl, s);
    func = mkapp(lvl, func, arg);
  }
  return func;
}

// rest_of_lambda ::= var* '.' exp
static ir parse_rest_of_lambda(char **cursor, size_t lvl, scope s) {
  if (!**cursor) {
    err_loc = *cursor;
    err_msg = "expected '.', got end of file";
    return NULL;
  }
  if (**cursor == '.') {
    ++*cursor;
    SKIP_WHITESPACE(cursor);
    return parse_exp(cursor, lvl, s);
  } else {
    char *name = *cursor;
    size_t name_len = parse_ident(cursor);
    if (!name_len) return NULL;
    struct a_scope_item extended = { name, name_len, s };
    ir body = parse_rest_of_lambda(cursor, lvl+1, &extended);
    if (!body) return NULL;
    return mkabs(lvl, body);
  }
}

// exp ::= '\' rest_of_lambda | 'λ' rest_of_lambda | atomic_exp atomic_exp*
static ir parse_exp(char **cursor, size_t lvl, scope s) {
  if (**cursor == '\\') {
    ++*cursor;
    SKIP_WHITESPACE(cursor);
    return parse_rest_of_lambda(cursor, lvl, s);
  } else if (strncmp(*cursor, "λ", sizeof("λ")) == 0) {
    *cursor += sizeof("λ") - 1;
    SKIP_WHITESPACE(cursor);
    return parse_rest_of_lambda(cursor, lvl, s);
  } else {
    ir func = parse_atomic_exp(cursor, lvl, s);
    if (!func) return NULL;
    while (**cursor && **cursor != ')') {
      ir arg = parse_atomic_exp(cursor, lvl, s);
      if (!arg) return NULL;
      func = mkapp(lvl, func, arg);
    }
    return func;
  }
}

/************** Compiler *************/

static uint8_t *code_buf_start;
static uint8_t *code_buf;
static uint8_t *code_buf_end;

static void init_code_buf(void) {
  // Need to mmap it so that I can mprotect it later.
  failwith("TODO");
}

static void write_header(uint32_t size, uint32_t tag) {
  // Align up to nearest word
  code_buf = (uint8_t *) (((size_t) code_buf + 7) & ~7);

  if (code_buf + 8 > code_buf_end) failwith("Too much code");

  memcpy(code_buf, &size, sizeof(uint32_t));
  code_buf += sizeof(uint32_t);
  memcpy(code_buf, &tag, sizeof(uint32_t));
  code_buf += sizeof(uint32_t);
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

// Copy registers X := Y
/* #define MOVRR(X, Y) REXW(Y,0,X), 0x89, (0xc0 | ((Y & 7) << 3) | (X & 7)) */

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

static void reg64(uint8_t opcode, enum reg reg, enum reg other_reg) {
  // Mod == 11: r/m
  CODE(
    REXW(reg, 0, other_reg),
    opcode,
    MODRM(3, reg, other_reg)
  );
}

static void add_imm(enum reg reg, int32_t imm) {
  if (-128 <= imm && imm < 128)
    CODE(REXW(0, 0, reg), 0x83, MODRM(3, 0, reg), (uint8_t) imm);
  else
    CODE(REXW(0, 0, reg), 0x81, MODRM(3, 0, reg), U32((uint32_t) imm));
}

static void *start_closure(size_t argc, size_t envc) {
  assert(argc < UINT_MAX);
  assert(envc < UINT_MAX);

  write_header(envc + 1, FUN);
  void *code_start = code_buf;

  CODE(
    // cmp %r15, argc
    0x4c, 0x39, 0x3c, 0x25, U32(argc),
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
  assert(envc < UINT_MAX);

  write_header(envc + 1, THUNK);
  void *code_start = code_buf;

  CODE(
    // test argc,argc (argc is %r15)
    0x4d, 0x85, 0xff,
    // jz adjacent_updates (+29)
    0x74, 29,
    // sub data_stack, $8 (data_stack is %r12)
    0x49, 0x83, 0xec, 0x08,
    // mov [data_stack], self (self is rbx)
    0x49, 0x89, 0x1c, 0x24,
    // push argc (argc is r15)
    0x41, 0x57,
    // call rest_of_code (+26)
    0xe8, U32(26),
    // pop argc (argc is %r15)
    0x41, 0x5f,
    // movabs rdi, rt_update_thunk
    0x48, 0xbf, U64((size_t) rt_update_thunk),
    // jmp rdi
    0xff, 0xe7,
    // FIXME wrong codegen
    // adjacent_updates:
    // movabs rdi, rt_avoid_adjacent_update_frames
    0x48, 0xbf, U64((size_t) rt_adjacent_update_frames),
    // call rdi
    0xff, 0xd7
    // rest_of_code:
  );

  return code_start;
}

static void make_sure_can_access_var(struct env *env, var v) {
  while (v < env->args_start && env->upvals[v].tag == UNUSED) {
    env->upvals[v].tag = USED;
    env->upvals[v].env_idx = env->envc++;
    env = env->up;
  }
}

static void load_arg(enum reg reg, size_t idx) {
  assert(idx < INT_MAX / 8);
  LOAD(reg, RSP, 8 * idx);
}
static void load_env_item(enum reg reg, size_t idx) {
  assert(idx < INT_MAX / 8);
  STORE(reg, SELF, 8 * idx + 8);
}

static void heap_check(size_t bytes_allocated) {
  // TODO: better maximum allocation size control
  assert(bytes_allocated < 1024 * 1024);

  add_imm(HEAP_PTR, - (int32_t) bytes_allocated);
  CODE(
    // cmp heap, heap limit (r13,r14)
    0x4d, 0x39, 0xf5,
    // jae alloc_was_good (offset depends on imm8 vs imm32)
    0x73, (bytes_allocated <= 128 ? 16 : 19),
    // movabs rdi, rt_gc
    0x48, 0xbf, U64((size_t) rt_gc),
    // call rdi
    0xff, 0xd7
  );
  add_imm(HEAP_PTR, - (int32_t) bytes_allocated);
  // alloc_was_good:
}

static void do_allocations(size_t lvl, struct env *this_env, size_t n, struct compile_result locals[n]) {
  size_t words_allocated = 0;
  for (size_t i = 0; i < n; i++)
    words_allocated += locals[i].env->envc + 1;

  heap_check(8 * words_allocated);

  MOV_RR(RDI, HEAP_PTR);
  for (size_t i = 0; i < n; i++) {
    add_imm(DATA_STACK, -8);
    STORE(RDI, DATA_STACK, 0);
    lvl--;

    // movabs rsi, entrypoint
    CODE(0x48, 0xbe, U64((uint64_t) locals[i].code));
    STORE(RSI, RDI, 0);

    struct env *env = locals[i].env;
    assert(env->up == this_env);
    size_t count = 0;
    for (var v = 0; v < env->args_start; v++) {
      switch (env->upvals[v].tag) {
      case UNUSED:
        break;
      case USED:
        count++;
        if (v >= this_env->args_start) {
          load_arg(RSI, lvl - v);
        } else {
          assert(this_env->upvals[v].tag == USED);
          load_env_item(RSI, this_env->upvals[v].env_idx);
        }
        size_t offset = 8 + 8*env->upvals[v].env_idx;
        STORE(RSI, RDI, offset);
        break;
      default:
        failwith("unreachable");
      }
    }
    assert(count == env->envc);

    if (i != n-1)
      add_imm(RDI, 8 + 8*env->envc);
  }
}

enum mov_state { NOT_DONE, IN_PROGRESS, DONE };

struct mov_item {
  enum { READ_ARG, READ_ENV, WRITE_ARG, WRITE_SELF } action;
  enum reg reg;
  int idx;
  struct mov_item *next;
};

typedef struct {
  struct mov_item *start;
  struct mov_item *end;
} mov_seg;

mov_seg singleton_mov_seg(int action, enum reg reg, int idx) {
  struct mov_item *ptr = (struct mov_item *) malloc(sizeof(struct mov_item));
  *ptr = (struct mov_item) {
    .action = action, .reg = reg, .idx = idx, .next = NULL };
  return (mov_seg) { .start = ptr, .end = ptr };
}

mov_seg append_mov_segs(mov_seg s1, mov_seg s2) {
  if (!s1.start)
    return s2;
  if (!s2.start)
    return s1;
  s1.end->next = s2.start;
  return (mov_seg) { .start = s1.start, .end = s2.end };
}

struct dest_info_item {
  enum { FROM_ARGS, FROM_ENV } src_type;
  int src_idx;
  int next_with_same_src; // -1 if none
  enum mov_state state;
};

// Returns either:
//  -1 if all went well
//  N if item N from the source is now in rdi
static int mov_one(
    size_t n,
    struct dest_info_item dest_info[n + 1],
    int src_to_dest[n + 1],
    int src,
    mov_seg *out) {

  *out = (mov_seg) { .start = NULL, .end = NULL };

  bool had_cycle_yet = false;
  bool in_rdi = false;


  for (int dest = src_to_dest[src]; dest != -1; dest = dest_info[dest].next_with_same_src) {
    // TODO:
    switch (dest_info[dest].state) {
      case ...:
    }
    mov_seg tmp;
    int item = mov_one(n, dest_info, src_to_dest, dest, &tmp);
    if (item != -1) {
      assert(!had_cycle_yet);
      had_cycle_yet = true;
      in_rdi = item == src;
      *out = append_mov_segs(*out, tmp);
    } else {
      *out = append_mov_segs(tmp, *out);
    }
  }

  if (src == n)
    do something special? since src == n is 'self'
    can src == n && in_rdi happen?
      means a cycle like arg0 -> self, env.y -> arg0
      handled in some particular way
      I think it can happen
      just means that env.y is in rdi
  if (in_rdi) {
    mov_seg store = singleton_mov_seg
    *out = append_mov_segs(*out, singleton_mov_seg())
  }
  failwith("TODO");
}

// TODO:
//
//  - [ ] Fix the thunk entry code
//  - [ ] Implement parallel move for shuffling args
//  - [ ] Tie it all together in a big codegen function
//  - [ ] *Really* tie everything together in a function which parses and codegens
//  - [ ] **Really** really tie everything together with a main function!

/* static struct comp_result *codegen(size_t lvl, struct env *up, ir *ir); */





