#include <stddef.h>

static volatile unsigned char sink;

__attribute__((noinline))
static void trigger_second_target(void) {
  unsigned char bytes[1] = {0};
  bytes[1] = 0x41;
  sink ^= bytes[0];
}

int main(void) {
  trigger_second_target();
  return (int)sink;
}
