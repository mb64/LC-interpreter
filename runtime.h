#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

typedef size_t word;
typedef uint32_t halfword;

#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

#define DEBUG(...) fprintf(stderr, __VA_ARGS__)
// #define DEBUG(...) ((void) 0)

typedef struct {
  halfword size, tag;
  word args[];
} obj;

enum { REF, THUNK, BLACKHOLE, CLOS, RIGID, FORWARD };

// runtime layout:
//
// REF ptr
// THUNK code env...
// BLACKHOLE <garbage>
// CLOS code env...
// RIGID var args...
// FORWARD ptr (only during GC)

enum { ARG, ENV };
enum {
  // first instruction in closures
  ARGC_CHECK,
  // allocation
  MKCLOS, MKTHUNK,
  // argc manipulation
  MORE_ARGS,  FEWER_ARGS,
  // stack manipulation
  MOV,
  READTMP, WRITETMP,
  // blackhole (penultimate instruction in thunks)
  BH_SELF,
  // the terminal instruction (always a tail call)
  CALL,

  // never appears in the code of actual closures/thunks
  THIS_IS_A_PARTIAL_APPLICATION
};

typedef const uint8_t code;

code PAP_ENTRY_CODE[] = { THIS_IS_A_PARTIAL_APPLICATION };

// code layout:
//
// var ::= ARG idx | ENV idx
// 
// instr ::=
//  MKCLOS  n code* var[n]
//  MKTHUNK n code* var[n]
//  MORE_ARGS n
//  FEWER_ARGS n
//  MOV var idx
//  READTMP idx
//  WRITETMP idx
//  CALL var (* always the last instruction *)
//
// code ::= instr*

// Regs:
//  - rsp ofc
//  - data stack (assume no overflow?)
//  - heap
//  - heap limit
//  - current closure env
//  - argc
//  - a couple temporary registers
//


// Simple generational semispace GC
// Allocations go downards
#define NURSERY_BYTES (1024*1024) // 1M nursery
word *nursery_start;
word *nursery_top;
#define IS_YOUNG(o) ((size_t) (o) - (size_t) nursery_start < NURSERY_BYTES)

word *old_start;
word *old_top;
size_t old_size_bytes;
size_t used_space_prev_gc;

// Remembered set: a growable (malloc'd) vector of old objects 'REF ptr' that
// point to the nursery
obj **remembered_set;
size_t remembered_set_size;
size_t remembered_set_cap;

// Copy stack: during GC, a worklist of new to-space objects whose fields still
// point to the from-space
obj **copy_stack;
size_t copy_stack_size;
size_t copy_stack_cap;

// Data stack, grows downwards. I assume it never overflows
// It contains GC roots and only GC roots
#define DATA_STACK_BYTES (8*1024*1024)
obj **data_stack_top;
obj **data_stack_start;

// The function/thunk currently being evaluated
obj *self;


void runtime_init(void);

obj *alloc(halfword size, halfword tag);
void minor_gc(void);
void major_gc(void);

enum gc_type { MAJOR, MINOR };
static void copy_to_old_space(obj **o, enum gc_type type);
static void process_copy_stack(enum gc_type type);
static void collect_roots(enum gc_type type);


// The top of the data stack holds a thunk to update with this value. Do that.
// (Includes write barrier)
void upd(obj *thunk, obj *val);


/* Force self:
 *  - if a thunk/ref, eval/deref it
 *  - then apply it to argc arguments, update the thunk, and set 'self' to the
 *    value
 */
void force(int argc);
/* Run self: self is a closure/thunk, run its code */
void run(int argc);



