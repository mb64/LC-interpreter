
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


