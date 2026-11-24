extern void reach_error(void);
extern void external_dispatch(void (*callback)(void));

static void callback(void)
{
    reach_error(); // Expected: unknown. The unmodelled dispatcher may call this.
}

int main(void)
{
    external_dispatch(callback);
}
