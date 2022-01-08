#ifndef FRONTEND_H
#define FRONTEND_H 1

#include <stddef.h>

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
  size_t lvl;
  size_t arity;
  letlist lets;
  letlist *lets_end;
  size_t lets_len;
  var head;
  arglist args;
} *ir;

/** Parse the given text, reporting errors to the user.
 *
 * If there's an error, it returns null
 *
 * Does not free the text
 */
ir parse(const char *text);

/** All IR is allocated from an arena.
 *
 * This function frees the arena
 */
void free_ir(void);

/** For debugging purposes
 */
void print_ir(ir term);

#endif // FRONTEND_H
