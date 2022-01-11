
all: Weak Strong

bench: Weak Strong
	hyperfine ./Weak ./Strong

GHCOPTS = -threaded -rtsopts -O2 -fllvm -fno-cse -fno-full-laziness

Weak: Weak.hs
	ghc $(GHCOPTS) --make Weak.hs -o Weak
Strong: Strong.hs
	ghc $(GHCOPTS) --make Strong.hs -o Strong

