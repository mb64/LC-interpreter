CFLAGS = -Wall -O2 -foptimize-sibling-calls -g

RT_OBJS = build/gc.o build/builtins.o build/normalize.o
OBJS = build/frontend.o build/backend.o build/main.o

lc: $(RT_OBJS) $(OBJS)
	gcc -o $@ $^

$(RT_OBJS): build/%.o: runtime/%.c build runtime/*.h
	gcc $(CFLAGS) -c $< -o $@
$(OBJS): build/%.o: %.c build *.h runtime/*.h
	gcc $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

.PHONY: bench
bench: lc
	hyperfine './lc "$$(cat bench.lc)"'
