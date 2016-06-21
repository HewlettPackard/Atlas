#include "makalu_internal.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h> 


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


void MAK_err_printf(const char *format, ...)
{
    va_list args;
    char buf[BUFSZ+1];

    va_start(args, format);
    buf[BUFSZ] = 0x15;

    (void) vsnprintf(buf, BUFSZ, format, args);
    va_end(args);
    if (buf[BUFSZ] != 0x15) ABORT("GC_printf clobbered stack");
    if (MAK_write(stderr, buf, strlen(buf)) < 0)
      ABORT("write to stderr failed");
    
}

MAK_INNER void MAK_warn_proc(char* msg, MAK_word arg){
    MAK_err_printf(msg, arg);
}

