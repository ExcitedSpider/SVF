#include "assert.h"

int main(void)
{
    int i;
    for (i = 0; i < 100; i++)
    {
    }
    __VERIFIER_assert(i == 100); // Expected: unreachable.
}
