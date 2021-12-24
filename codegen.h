#include <stddef.h>

typedef struct code code;

void codegen_init(void);

code *codegen_start(void);
void codegen_argc_check(size_t argc);
void codegen_mkclos_start(size_t envc, code *entrypoint);
void codegen_mkthunk_start(size_t envc, code *entrypoint);
void codegen_env_add_from_args(size_t idx);
void codegen_env_add_from_env(size_t idx);
void codegen_more_args(size_t n);
void codegen_fewer_args(size_t n);
void codegen_mov_from_args(size_t src, size_t dest);
void codegen_mov_from_env(size_t src, size_t dest);
void codegen_readtmp(size_t idx);
void codegen_writetmp(size_t idx);
void codegen_blackhole(void);
void codegen_set_self_from_args(size_t idx);
void codegen_set_self_from_env(size_t idx);
void codegen_set_self_from_tmp(void);
void codegen_static_call(code *entrypoint);
void codegen_dynamic_call(void);

/** what it looks like:
 *
 * 1. argc check (only in closures)
 * 2. heap check maybe?
 * 3. zero or more of: allocate a {closure,thunk} and push it to the data stack
 * 5. shuffle the self pointer and stuff on the data stack
 *    → just prior to updating self, blackhole self (only in thunks)
 * 6. jump to force
 *    → if it's know to be a let-bound lambda, we could jump to its code instead
 */



