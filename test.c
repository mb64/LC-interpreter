#include "runtime.h"

obj *cons(size_t value, obj *next) {
  obj *o = alloc(2, RIGID);
  o->args[0] = value;
  o->args[1] = (word) next;
  return o;
}

obj *nil(void) {
  obj *o = alloc(1, RIGID);
  o->args[0] = 0;
  return o;
}

int main() {
  runtime_init();
  self = nil();
  for (int i = 0; i < 500000000; i++) {
    self = cons(i, self);
    if (i % 30000 == 0)
      self = nil();
  }
}

void run(int) {}
