#include "assert.h"

int main(void)
{
    // Bound both inputs so all additions are defined.
    int a = __VERIFIER_nondet_int() & 15;
    int b = __VERIFIER_nondet_int() & 15;
    int c = a + b;
    __VERIFIER_assert(a + b == c); // Expected: unknown.
}
