#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>


#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

/*************** IR **************/

typedef size_t var;

typedef struct a_let {
  struct exp *val;
  struct a_let *next;
} *letlist;

typedef struct an_arg {
  struct an_arg *prev;
  var arg;
} *arglist;

typedef struct exp {
  size_t arity;
  letlist lets;
  letlist *lets_end;
  size_t lets_len;
  var head;
  arglist args;
} *ir;

static bool is_var(ir e);
static bool is_lambda(ir e);

static ir mkvar(size_t lvl, var v);
static ir mkapp(size_t lvl, ir func, ir arg);
static ir mkabs(size_t lvl, ir body);

/*************** Parser ***************/

/** Parse the given text, reporting errors to the user.
 *
 * If there's an error, it returns null
 */
static ir parse(char *text);

typedef struct a_scope_item {
  const char *name;
  size_t name_len;
  struct a_scope_item *next;
} *scope;

static bool skip_whitespace(char **cursor);
#define SKIP_WHITESPACE(cursor) \
  if (!skip_whitespace(cursor)) return 0
static size_t parse_ident(char **cursor);
static ir parse_var(char **cursor, size_t lvl, scope s);
static ir parse_exp(char **cursor, size_t lvl, scope s);
static ir parse_atomic_exp(char **cursor, size_t lvl, scope s);
static ir then_parse_some_args(char **cursor, size_t lvl, scope s, ir func);
static ir parse_rest_of_lambda(char **cursor, size_t lvl, scope s);


/************** Compiler *************/

// TODO

static void init_code_buf(void);

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
#define MOV_RR(dest, src) reg64(OP_STORE, src, dest);

static void add_imm(enum reg reg, int32_t imm);

static void *start_closure(size_t argc, size_t envc);
static void *start_thunk(size_t envc);

struct var_info {
  enum { UNUSED, USED } tag;
  // Only when USED
  size_t env_idx;
};

struct env {
  struct env *up;
  var args_start;
  size_t envc;
  // args_start elements, envc of which are ENV and the others are UNUSED
  struct var_info upvals[];
};

struct compile_result {
  void (*code)(void);
  struct env *env;
};

static void make_sure_can_access_var(struct env *env, var v);

static void load_arg(enum reg reg, size_t idx);
static void load_env_item(enum reg reg, size_t idx);

static void do_allocations(size_t lvl, struct env *this_env, size_t n, struct compile_result locals[n]);
static void heap_check(size_t bytes_allocd);






