extern void reach_error(void);

int main(void)
{
    int x = 12;
    if (x > 10 && x < 5)
        reach_error(); // Expected: unreachable. The second comparison is false.
}
