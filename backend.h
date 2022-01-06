#include <stddef.h>
#include "frontend.h"

/** Compile a top-level (closed, at level 0) term to machine code.
 *
 * It returns a void *. This is not executable until codegen_finalize is run.
 */
void *compile_toplevel(ir term);

/** Remap the codegen'd area from RW to RX.
 *
 * You may now cast the void *'s from codegen_toplevel to void(*)(void)
 */
void compile_finalize(void);
