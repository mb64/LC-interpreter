#include "codegen.h"
#include "runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// code layout:
//
// var ::= ARG idx | ENV idx
// 
// instr ::=
//  ARGC_CHECK n
//  MKCLOS  n code* var[n]
//  MKTHUNK n code* var[n]
//  MORE_ARGS n
//  FEWER_ARGS n
//  MOV var idx
//  READTMP idx
//  WRITETMP idx
//  BH_SELF
//  SET_SELF var
//  SET_SELF_TO_TMP
//  DYNCALL
//  KNOWNCALL code*
//
// code ::= instr*

typedef uint8_t bytecode;

enum { ARG, ENV };
enum {
  // first instruction in closures
  ARGC_CHECK,
  // allocation
  MKCLOS, MKTHUNK,
  // argc manipulation
  MORE_ARGS, FEWER_ARGS,
  // self and stack manipulation
  MOV,
  READTMP, WRITETMP,
  SET_SELF, SET_SELF_TO_TMP,
  // blackhole (before updating self in thunks)
  BH_SELF,
  // terminal instruction (always a tail call)
  DYNCALL, KNOWNCALL,

  // never appears in the code of actual closures/thunks
  THIS_IS_A_PARTIAL_APPLICATION
};

/***************** Compile time ***************/

#define BC_ARENA_SIZE (8 * 1024 * 1024)
static bytecode *bc_arena = NULL;
static bool in_progress = false;

void codegen_init(void) {
  if (!bc_arena)
    bc_arena = (bytecode *) malloc(BC_ARENA_SIZE);
}

code *codegen_start(void) {
  assert(!in_progress);
  in_progress = true;
  return (code *) bc_arena;
}

void codegen_argc_check(size_t argc) {
  assert(argc < 256);
  *bc_arena++ = ARGC_CHECK;
  *bc_arena++ = (uint8_t) argc;
}
void codegen_mkclos_start(size_t envc, code *entrypoint) {
  assert(envc < 256);
  *bc_arena++ = MKCLOS;
  *bc_arena++ = (uint8_t) envc;
  memcpy(bc_arena, &entrypoint, sizeof(code *));
  bc_arena += sizeof(code *);
}
void codegen_mkthunk_start(size_t envc, code *entrypoint) {
  // TODO: dedup with mkclos_start
  assert(envc < 256);
  *bc_arena++ = MKTHUNK;
  *bc_arena++ = (uint8_t) envc;
  memcpy(bc_arena, &entrypoint, sizeof(code *));
  bc_arena += sizeof(code *);
}
void codegen_env_add_from_args(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = ARG;
  *bc_arena++ = (uint8_t) idx;
}
void codegen_env_add_from_env(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = ENV;
  *bc_arena++ = idx;
}
void codegen_more_args(size_t n) {
  assert(n < 256);
  *bc_arena++ = MORE_ARGS;
  *bc_arena++ = (uint8_t) n;
}
void codegen_fewer_args(size_t n) {
  assert(n < 256);
  *bc_arena++ = FEWER_ARGS;
  *bc_arena++ = (uint8_t) n;
}
void codegen_mov_from_args(size_t src, size_t dest) {
  assert(src < 256 && dest < 256);
  *bc_arena++ = MOV;
  *bc_arena++ = ARG;
  *bc_arena++ = (uint8_t) src;
  *bc_arena++ = (uint8_t) dest;
}
void codegen_mov_from_env(size_t src, size_t dest) {
  assert(src < 256 && dest < 256);
  *bc_arena++ = MOV;
  *bc_arena++ = ENV;
  *bc_arena++ = (uint8_t) src;
  *bc_arena++ = (uint8_t) dest;
}
void codegen_readtmp(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = READTMP;
  *bc_arena++ = (uint8_t) idx;
}
void codegen_writetmp(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = WRITETMP;
  *bc_arena++ = (uint8_t) idx;
}
void codegen_blackhole(void) {
  *bc_arena++ = BH_SELF;
}
void codegen_set_self_from_args(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = SET_SELF;
  *bc_arena++ = ARG;
  *bc_arena++ = (uint8_t) idx;
}
void codegen_set_self_from_env(size_t idx) {
  assert(idx < 256);
  *bc_arena++ = SET_SELF;
  *bc_arena++ = ENV;
  *bc_arena++ = (uint8_t) idx;
}
void codegen_set_self_from_tmp(void) {
  *bc_arena++ = SET_SELF_TO_TMP;
}
void codegen_static_call(code *entrypoint) {
  *bc_arena++ = KNOWNCALL;
  memcpy(bc_arena, &entrypoint, sizeof(code *));
  bc_arena += sizeof(code *);
  in_progress = false;
}
void codegen_dynamic_call(void) {
  *bc_arena++ = DYNCALL;
  in_progress = false;
}

/*************** Runtime ***************/

bytecode PAP_ENTRY_CODE[] = { THIS_IS_A_PARTIAL_APPLICATION };

void run(int argc) {
  assert(self->tag == THUNK || self->tag == CLOS);
  bytecode *pc = (bytecode *) self->args[0];
  obj *tmp = NULL;
# define VAR(off) \
    ((pc[off] == ARG ? data_stack_top : (obj **) self->args + 1)[pc[off+1]])
  for (;;) switch(*pc) {
  case ARGC_CHECK:
    {
      int params = pc[1];
      if (argc < params) {
        // Package it up in a partial application and return
        obj *pap = alloc(argc + 2, CLOS);
        pap->args[0] = (word) PAP_ENTRY_CODE;
        pap->args[1] = (word) self;
        memcpy(pap->args + 2, data_stack_top, argc * sizeof(obj *));
        data_stack_top += argc;
        upd(*data_stack_top++, pap);
        self = pap;
        return;
      }
      pc += 2;
      break;
    }
  case MKCLOS:
  case MKTHUNK:
    {
      int tag = *pc == MKCLOS ? CLOS : THUNK;
      int envc = pc[1];
      obj *o = alloc(envc + 1, tag);
      memcpy(&o->args[0], pc + 2, sizeof(word)); // memcpy since it's not properly aligned
      pc += 2 + sizeof(word);
      // add stuff to the env
      for (int i = 0; i < envc; i++)
        o->args[i + 1] = (word) VAR(2 * i);
      pc += 2 * envc;
      break;
    }
  case MORE_ARGS:
    {
      size_t n = pc[1];
      data_stack_top -= n;
#     ifndef NDEBUG
        memset(data_stack_top, 0, n * sizeof(obj *));
#     endif
      argc += n;
      pc += 2;
      break;
    }
  case FEWER_ARGS:
    {
      size_t n = pc[1];
      data_stack_top += n;
      argc -= n;
      pc += 2;
      break;
    }
  case MOV:
    {
      obj *o = VAR(1);
      data_stack_top[pc[3]] = o;
      pc += 4;
      break;
    }
  case READTMP:
    {
      int idx = pc[1];
      tmp = data_stack_top[idx];
      pc += 2;
      break;
    }
  case WRITETMP:
    {
      int idx = pc[1];
      data_stack_top[idx] = tmp;
      pc += 2;
      break;
    }
  case SET_SELF:
    {
      self = VAR(1);
      pc += 3;
      break;
    }
  case SET_SELF_TO_TMP:
    {
      self = tmp;
      pc++;
      break;
    }
  case BH_SELF:
    {
      self->tag = BLACKHOLE;
      self->size = 1;
      pc++;
      break;
    }
  case DYNCALL:
    {
      return force(argc);
    }
  case KNOWNCALL:
    {
      bytecode *dest;
      memcpy(&dest, pc+1, sizeof(bytecode *));
      assert(self->tag == CLOS && (bytecode *) self->args[0] == dest);
      pc = dest;
      break;
    }
  case THIS_IS_A_PARTIAL_APPLICATION:
    {
      // In compiled code, this won't be compiled into closures, but rather a
      // built-in function in the runtime
      assert(self->args[0] == (word) PAP_ENTRY_CODE);

      // Add all the stuff as arguments and jump to the contained closure
      obj *clos = (obj *) self->args[1];
      int extra_args = self->size - 2;
      data_stack_top -= extra_args;
      memcpy(data_stack_top, self->args + 2, extra_args * sizeof(obj *));
      self = clos;
      return run(argc + extra_args);
    }
  default:
    failwith("unreachable");
  }
# undef VAR
}

