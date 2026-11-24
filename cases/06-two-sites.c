#include "assert.h"

int main(void)
{
    __VERIFIER_assert(0); // Expected: reachable.
    int x = __VERIFIER_nondet_int();
    __VERIFIER_assert(x > 0); // Expected: unknown.
}
