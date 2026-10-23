#include <string.h>

extern void svf_assert(int);
static char global[4];

int main(void)
{
    char local[4];
    char cleared[4];
    char copied[4];
    local[1] = 7;
    svf_assert(local[1] == 7);
    global[2] = 9;
    svf_assert(global[2] == 9);
    memset(cleared, 0, sizeof(cleared));
    svf_assert(cleared[0] == 0);
    memcpy(copied, "abc", sizeof(copied));
    svf_assert(copied[0] == 'a');
    svf_assert(copied[2] == 'c');
    svf_assert(strlen(copied) == 3);
    return 0;
}
