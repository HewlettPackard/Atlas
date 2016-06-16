#include "makalu_internal.h"

#include <stdio.h>
#include <string.h>


MAK_INLINE int MAK_write(FILE *f, const char *buf, size_t len)
{
    int res = fwrite(buf, 1, len, f);
    fflush(f);
    return res;
}


MAK_INNER void MAK_abort(const char *msg)
{
    if (MAK_write(stderr, (void *)msg, strlen(msg)) >= 0)
        (void)MAK_write(stderr, (void *)("\n"), 1);

    (void) abort();
}

