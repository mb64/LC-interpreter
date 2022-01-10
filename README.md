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

## Simple benchmarks

The file `bench.lc` contains a lambda calculus program that does some Church
numeral math, with normal form `λ s z. z`.

On my computer, this interpreter normalizes it in 3.5 seconds, while the same
term implemented in Haskell with deeply-embedded HOAS is normalized in around 8
seconds.

It would be very interesting to compare to Coq's `vm_compute` and
`native_compute`; unfortunately, this requires a benchmark term that:
 - is typeable in COC
 - has similar runtime characteristics under both CBV and CBN

and I haven't come up with one yet.

## Future things

Really ought to have:
 - [x] Big enough tests to actually test the GC
 - [ ] Standardize a global term size limit
 - [ ] mmap the data stack with a guard page below it
 - [x] Make sure allocations are inlined in the runtime
 - [x] Benchmarks
 - [ ] Fix the `-Wstrict-aliasing` warnings

Want to have:
 - Add support for `fix`, let/letrec
 - Nice CLI and REPL

Probably won't have but would be cool:
 - Use it in some algorithm that requires strong normalization, like a dependent
   typechecker or possibly supercompilation by evaluation



