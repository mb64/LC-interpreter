#ifndef DATA_LAYOUT_H
#define DATA_LAYOUT_H 1

#include <stdint.h>

/************* Object layout ***********/

typedef size_t word;
typedef uint32_t halfword;

typedef struct obj {
  void (*entrypoint)(void);
  word contents[];
} obj;

// Can't be an enum since I need to include them in inline assembly
#define FORWARD   0
#define REF       1
#define FUN       2
#define PAP       3
#define RIGID     4
#define THUNK     5
#define BLACKHOLE 6

struct gc_data {
  /** Size of the whole object in words.
   * If 0, the first word of contents is a `struct info_word` that contains
   * the true size
   */
  uint32_t size;
  uint32_t tag;
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

#endif // DATA_LAYOUT_H
