#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "data_layout.h"

/************* Random utils ***********/

#define failwith(...) do { fprintf(stderr, __VA_ARGS__); abort(); } while (0)

// #define DEBUG(...) fprintf(stderr, __VA_ARGS__)
#define DEBUG(...) ((void) 0)

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

