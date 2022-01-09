#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "frontend.h"
#include "backend.h"
#include "runtime/normalize.h"

int main(int argc, const char **argv) {
  const char *source = argc >= 2 ? argv[1] : "λ x. x";

  printf("Parsing %s\n", source);
  ir term = parse(source);

  printf("Parsed: ");
  print_ir(term);

  printf("Compiling\n");
  void *code = compile_toplevel(term);

  free_ir();

  printf("Mapping as executable\n");
  compile_finalize();
  void (*entrypoint)(void) = code;

  printf("Normalizing\n");
  unsigned int *nf = normalize(entrypoint);

  printf("Normal form: ");
  print_normal_form(nf);

  free(nf);

}

