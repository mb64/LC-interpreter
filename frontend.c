#include "runtime.h"
#include "frontend.h"

/************** IR **************/

static word *ir_arena;
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

// TODO


