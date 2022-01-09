# A λ-calculus interpreter

A strongly-normalizing interpreter for the untyped lambda calculus.

```shell
$ make
$ ./lc "(λ x y. x) (λ x. x)"
Input: (λ x y. x) (λ x. x)
Compiling... Compiled! Normalizing...
Normal form: λ a b. b
```

## Features

 - Generational copying GC, with a dynamically sized old space
 - Custom strongly normalizing lazy evaluation runtime
 - Compiles lambda terms to x86\_64 machine code

Only tested on Linux, and it only supports x86\_64.

## Future things

Really ought to have:
 - Big enough tests to actually test the GC
 - Add a global term size limit
 - mmap the data stack with a guard page below it
 - Make sure allocations are inlined in the runtime
 - Benchmarks

Want to have:
 - Add support for `fix`, let/letrec
 - Nice CLI and REPL

Probably won't have but would be cool:
 - Use it in some algorithm that requires strong normalization, like a dependent
   typechecker or possibly supercompilation by evaluation


