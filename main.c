#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "frontend.h"
#include "backend.h"
#include "runtime/normalize.h"

int main(int argc, const char **argv) {
  const char *source = "λ x. x";

  printf("Parsing %s\n", source);
  ir term = parse(source);

  printf("Compiling\n");
  void *code = compile_toplevel(term);

  free_ir();

  printf("Mapping as executable\n");
  compile_finalize();
  void (*entrypoint)(void) = code;

  printf("Normalizing\n");
  unsigned int *nf = normalize(entrypoint);

  printf("And here's the normal form:\n");
  print_normal_form(nf);

  free(nf);

}

