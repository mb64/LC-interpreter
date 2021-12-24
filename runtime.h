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

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) ((void) 0)

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

// Regs:
//  - rsp ofc
//  - data stack (assume no overflow?)
//  - heap
//  - heap limit
//  - current closure env
//  - argc
//  - a couple temporary registers
//

// Data stack, grows downwards. I assume it never overflows
// It contains GC roots and only GC roots
#define DATA_STACK_BYTES (8*1024*1024)
extern obj **data_stack_top;
extern obj **data_stack_start;

// The function/thunk currently being evaluated
extern obj *self;


void runtime_init(void);

obj *alloc(halfword size, halfword tag);
void minor_gc(void);
void major_gc(void);

void upd(obj *thunk, obj *value);

/* Force self:
 *  - if a thunk/ref, eval/deref it
 *  - then apply it to argc arguments, update the thunk, and set 'self' to the
 *    value
 */
void force(int argc);

/* Run self: self is a closure/thunk, run its code */
void run(int argc);



