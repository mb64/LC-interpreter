#include "frontend.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

/**************** Lowering to IR ****************/

static void arena_init(void);

static bool is_var(ir e);
static bool is_lambda(ir e);

static ir mkvar(size_t lvl, var v);
static ir mkapp(size_t lvl, ir func, ir arg);
static ir mkabs(size_t lvl, ir body);

static size_t *ir_arena_start = NULL;
static size_t *ir_arena = NULL;
static size_t *ir_arena_end = NULL;
#define IR_ARENA_SIZE (32 * 1024 * 1024)

static void arena_init(void) {
  if (!ir_arena) {
    ir_arena_start = ir_arena = malloc(IR_ARENA_SIZE);
    ir_arena_end = ir_arena + IR_ARENA_SIZE / sizeof(*ir_arena);
  }
}

void free_ir(void) {
  free(ir_arena_start);
  ir_arena = ir_arena_end = NULL;
}

#define ARENA_ALLOC(ty, ...) \
  ty *node = (ty *) ir_arena; \
  ir_arena += sizeof(ty) / sizeof(*ir_arena); \
  if (ir_arena > ir_arena_end) failwith("Too much code"); \
  *node = (ty) { __VA_ARGS__ }; \
  return node;

static arglist snoc_arg(arglist prev, var arg) {
  ARENA_ALLOC(struct an_arg, prev, arg)
}
static letlist cons_let(ir val, letlist next) {
  ARENA_ALLOC(struct a_let, val, next)
}

static ir mkvar(size_t lvl, var v) {
  ARENA_ALLOC(struct exp, lvl, 0, NULL, NULL, 0, v, NULL);
}
#undef ARENA_ALLOC

static bool is_var(ir e) {
  return e->arity == 0 && e->lets == NULL && e->args == NULL;
}
static bool is_lambda(ir e) {
  return e->arity > 0;
}

static ir mkabs(size_t lvl, ir body) {
  assert(body->lvl == lvl + 1);
  body->lvl = lvl;
  body->arity++;
  return body;
}

static ir mkapp(size_t lvl, ir func, ir arg) {
  if (is_lambda(func)) {
    if (is_var(arg)) {
      // Applying a lambda to a var: let f = func in f x
      // f becomes lvl
      ir res = mkvar(lvl, lvl);
      res->lets = cons_let(func, NULL);
      res->lets_end = &res->lets->next;
      res->lets_len = 1;
      res->args = snoc_arg(NULL, arg->head);
      return res;
    } else {
      // Applying a lambda to a complex thing: let f = func ; x = arg in f x
      // f becomes lvl, x becomes (lvl+1)
      ir res = mkvar(lvl, lvl);
      res->lets = cons_let(arg, NULL);
      res->lets_end = &res->lets->next;
      res->lets = cons_let(func, res->lets);
      res->lets_len = 2;
      res->args = snoc_arg(NULL, lvl+1);
      return res;
    }
  } else if (is_var(arg)) {
    // Applying a thunk to a var:
    //  (let ... in f args) x  ⇒  let ... in f args x
    var v = arg->head;
    func->args = snoc_arg(func->args, v);
    return func;
  } else {
    // Appying a thunk to something complex:
    //  (let ... in f args) arg ⇒ let ... ; x = arg in f args x
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

typedef struct a_scope_item {
  const char *name;
  size_t name_len;
  struct a_scope_item *next;
} *scope;

static bool skip_whitespace(const char **cursor);
#define SKIP_WHITESPACE(cursor) \
  if (!skip_whitespace(cursor)) return 0

static size_t parse_ident(const char **cursor);
static ir parse_var(const char **cursor, size_t lvl, scope s);
static ir parse_exp(const char **cursor, size_t lvl, scope s);
static ir parse_atomic_exp(const char **cursor, size_t lvl, scope s);
static ir parse_rest_of_lambda(const char **cursor, size_t lvl, scope s);

static const char *err_msg = NULL;
static const char *err_loc = NULL;


ir parse(const char *text) {
  arena_init();
  const char *cursor = text;
  skip_whitespace(&cursor);
  ir result = parse_exp(&cursor, 0, NULL);
  if (result && *cursor != '\0') {
    result = NULL;
    err_loc = text;
    err_msg = "expected eof";
  }

  if (!result)
    // TODO: better error message printing
    printf("parse error at byte %ld :/\n%s\n", err_loc - text, err_msg);

  return result;
}

static bool skip_whitespace(const char **cursor) {
  const char *text = *cursor;
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
      // Fall through
    default:
      *cursor = text;
      return true;
  }
}

// returns the length of the ident starting at *cursor
static size_t parse_ident(const char **cursor) {
  // parse [a-zA-Z_]+
  const char *start = *cursor;
  const char *end = start;
#define IDENT_CHAR(c) (('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '_')
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

static ir parse_var(const char **cursor, size_t lvl, scope s) {
  const char *start = *cursor;
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
static ir parse_atomic_exp(const char **cursor, size_t lvl, scope s) {
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

// rest_of_lambda ::= var* '.' exp
static ir parse_rest_of_lambda(const char **cursor, size_t lvl, scope s) {
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
    const char *name = *cursor;
    size_t name_len = parse_ident(cursor);
    if (!name_len) return NULL;
    struct a_scope_item extended = { name, name_len, s };
    ir body = parse_rest_of_lambda(cursor, lvl+1, &extended);
    if (!body) return NULL;
    return mkabs(lvl, body);
  }
}

// exp ::= '\' rest_of_lambda | 'λ' rest_of_lambda | atomic_exp atomic_exp*
static ir parse_exp(const char **cursor, size_t lvl, scope s) {
  if (**cursor == '\\') {
    ++*cursor;
    SKIP_WHITESPACE(cursor);
    return parse_rest_of_lambda(cursor, lvl, s);
  } else if (strncmp(*cursor, "λ", sizeof("λ") - 1) == 0) {
    *cursor += sizeof("λ") - 1;
    SKIP_WHITESPACE(cursor);
    return parse_rest_of_lambda(cursor, lvl, s);
  } else {
    ir func = parse_atomic_exp(cursor, lvl, s);
    if (!func) return NULL;
    // Then parse some args
    while (**cursor && **cursor != ')') {
      ir arg = parse_atomic_exp(cursor, lvl, s);
      if (!arg) return NULL;
      func = mkapp(lvl, func, arg);
    }
    return func;
  }
}

/*************** Pretty-printer **************/

static void print_var(var v);
static void print_lets(size_t lvl, letlist lets);
static void print_args(arglist args);
static void print_term(ir term);

static void print_var(var v) {
  printf("x_%lu", v);
}
static void print_lets(size_t lvl, letlist lets) {
  for (; lets; lets = lets->next, lvl++) {
    printf("let ");
    print_var(lvl);
    printf(" = ");
    print_term(lets->val);
    printf(" in ");
  }
}
static void print_args(arglist args) {
  if (args) {
    print_args(args->prev);
    printf(" ");
    print_var(args->arg);
  }
}
static void print_term(ir term) {
  if (!term->arity && !term->lets && !term->args) {
    // Just a var -- no parens necessary
    print_var(term->head);
    return;
  }
  printf("(");
  if (term->arity) {
    printf("λ");
    for (var v = term->lvl; v < term->lvl + term->arity; v++) {
      printf(" ");
      print_var(v);
    }
    printf(". ");
  }
  print_lets(term->lvl + term->arity, term->lets);
  print_var(term->head);
  print_args(term->args);
  printf(")");
}

void print_ir(ir term) {
  print_term(term);
  printf("\n");
}


