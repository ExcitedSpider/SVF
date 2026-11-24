#include "assert.h"

int main(void)
{
    int x = __VERIFIER_nondet_int();
    if (x > 10 && x < 5)
        reach_error(); // Expected: unreachable.
}
