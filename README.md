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

## What?

Normalizing a term in the λ-calculus means applying the β-reduction rule until
it can't be simplified any more.  While it's usually described using syntactic
substitution, the fastest algorithm in practice (normalization by evaluation)
involves running it like a regular functional programming language, with a
catch: we need to run programs with free variables in them, requiring some
special runtime support.

There are, of course, many ways to run a regular functional programming
language: eager vs lazy evaluation, interpreted vs bytecode interpreted vs
compiled to machine code, etc.  This implementation uses lazy evaluation and
compiles to machine code.

## How?

Short description of the lazy evaluation implementation strategy:
 - Push/enter, like Clean and old GHC
 - Closures instead of supercombinators, like GHC
 - Separate data stack + call stack, like Clean
 - Every heap object's a closure, like GHC
 - Eager blackholing
 - Thunk entry code pushes its own update frame, like GHC
 - Collapsing adjacent update frames is handled by the thunk entry code

It has a two-pass compiler and a simple generational copying GC.


## Simple benchmarks

The file `bench.lc` contains a lambda calculus program that does some Church
numeral math, with normal form `λ s z. z`.  The same lambda term is implemented
in Haskell in different ways in `haskell/{Strong,Weak}.hs`.

Here are the results, on my computer:

 - **This interpreter**: 3.5 seconds; allocates 5,854,199,808 bytes
 - **Haskell, `Strong.hs`:** 2.5 seconds; allocates 8,609,423,616 bytes

   This would perform comparably to a Haskell-based implementation of the
   "Tagged normalization" presented in the paper about Coq's `native_compute`.

 - **Haskell, `Weak.hs`:** 2.3 seconds; allocates 6,887,761,200 bytes

   This would perform comparably to a Haskell-based implementation of the
   `native_compute` paper's "Untagged normalization".

Despite allocating a lot, this lambda term uses very little space: GHC reports
~50K max residency in both cases (and it's similar for my interpreter, but I
haven't measured).

The extra 1.7GB that the tagged implementation allocates compared to the
untagged implementation come from two sources: both the overhead of including
tags, and the overhead of currying not being optimized. I'm surprised the
overhead is so little.

This interpreter and the untagged implementation should make pretty much the
same allocations, so I think the extra gigabyte that `Weak.hs` allocates comes
from two cases where GHC's heap objects are larger than mine:
 - GHC's thunks all contain an extra word, for lock-free concurrency reasons
 - GHC's partial application objects use a 3-word header, while mine use only 2
   words

IMO the main takeaway from this benchmark is that for purely functional
languages, **the garbage collector is key**.  How currying is done, how
arguments are passed, etc., all matter much less than having a high-quality
garbage collector.

The one area where this interpreter beats GHC is compile time: it compiles the
term practically instantly.


## Future things

Really ought to have:
 - [x] Big enough tests to actually test the GC
 - [ ] Standardize a global term size limit
 - [ ] mmap the data stack with a guard page below it
 - [x] Make sure allocations are inlined in the runtime
 - [x] Benchmarks
 - [ ] Fix the `-Wstrict-aliasing` warnings
 - [ ] Document the runtime better

Want to have:
 - Add support for `fix`, let/letrec
 - Nice CLI and REPL

Probably won't have but would be cool:
 - Use it in some algorithm that requires strong normalization, like a dependent
   typechecker or possibly supercompilation by evaluation



