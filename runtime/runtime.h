#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

/************* Random utils ***********/

typedef size_t word;
typedef uint32_t halfword;

#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) ((void) 0)

/************* Object layout ***********/

typedef struct obj {
  void (*entrypoint)(void);
  word contents[];
} obj;

enum gc_tag { FORWARD, REF, FUN, PAP, RIGID, THUNK, BLACKHOLE };

struct gc_data {
  /** Size of the whole object in words.
   * If 0, the first word of contents is a `struct info_word` that contains
   * the true size
   */
  uint32_t size;
  /* enum gc_tag */ uint32_t tag;
};
#define GC_DATA(o) \
  ((struct gc_data *) ((size_t) (o->entrypoint) - sizeof(struct gc_data)))

struct info_word {
  /** Size of the whole object in words */
  uint32_t size;
  /** only in heap objects representing rigid terms */
  uint32_t var;
};
#define INFO_WORD(o) ((struct info_word *) &o->contents[0])


/************** Registers **************/

// The function/thunk currently being evaluated
register obj *self asm ("rbx");

// Data stack, grows downwards. I assume it never overflows
// It contains GC roots
#define DATA_STACK_BYTES (8*1024*1024)
register obj **data_stack asm ("r12");

// Simple generational semispace GC
// Allocations go downards
#define NURSERY_BYTES (3*1024*1024) // 3M nursery
register word *nursery_top asm ("r13");
register word *nursery_start asm ("r14");
#define IS_YOUNG(o) ((size_t) (o) - (size_t) nursery_start < NURSERY_BYTES)

register size_t argc asm ("r15");

