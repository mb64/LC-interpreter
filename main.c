#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "frontend.h"
#include "backend.h"
#include "runtime/normalize.h"

int main(int argc, const char **argv) {
  const char *source = argc >= 2 ? argv[1] : "λ x. x";

  printf("Input: %s\n", source);
  printf("Compiling... ");
  fflush(stdout);

  ir term = parse(source);
  void *code = compile_toplevel(term);
  compile_finalize();
  free_ir();

  printf("Compiled! Normalizing...\n");
  unsigned int *nf = normalize(code);

  printf("Normal form: ");
  print_normal_form(nf);

  free(nf);
}

