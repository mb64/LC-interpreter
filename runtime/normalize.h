/** β-normalization of lambda terms
 *
 * It pre-order serializes the normal form as a malloc'd vector of unsigned
 * ints, with this layout:
 *  nf   ::= LAM nf | NE argc var (argc nf's)
 *  argc ::= an integer number of arguments
 *  var  ::= an integer de Bruijn level
 *
 */

enum nf_tag { LAM, NE };

// entrypoint is the entry code for a thunk with no environment representing the
// lambda term
unsigned int *normalize(void (*entrypoint)(void));

void print_normal_form(unsigned int *nf);


