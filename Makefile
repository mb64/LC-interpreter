CFLAGS = -Wall -Wextra -O -g

RT_OBJS = build/gc.o build/builtins.o build/normalize.o
OBJS = FIXME

lc: $(RT_OBJS) $(OBJS)
	gcc -o $@ $^

$(RT_OBJS): build/%.o: runtime/%.c build runtime/*.h
	gcc -Wall -Wextra -O -g -c $< -o $@
$(OBJS): build/%.o: %.c build *.h runtime/*.h
	gcc -Wall -Wextra -O -g -c $< -o $@

build:
	mkdir -p build
