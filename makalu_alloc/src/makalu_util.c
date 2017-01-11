/*
 * (c) Copyright 2016 Hewlett Packard Enterprise Development LP
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version. This program is
 * distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details. You should have received a copy of the GNU Lesser
 * General Public License along with this program. If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * Source code is partially derived from Boehm-Demers-Weiser Garbage 
 * Collector (BDWGC) version 7.2 (license is attached)
 *
 * File:
 *   misc.c
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1999-2001 by Hewlett-Packard Company. All rights reserved.
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 *
 */

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
    if (buf[BUFSZ] != 0x15) ABORT("MAK_printf clobbered stack");
    if (MAK_write(stderr, buf, strlen(buf)) < 0)
      ABORT("write to stderr failed");
    
}

/* A version of printf that is unlikely to call malloc, and is thus safer */
/* to call from the collector in case malloc has been bound to MAK_malloc. */
/* Floating point arguments and formats should be avoided, since fp       */
/* conversion is more likely to allocate.                                 */
/* Assumes that no more than BUFSZ-1 characters are written at once.      */
void MAK_printf(const char *format, ...)
{
    va_list args;
    char buf[BUFSZ+1];

    va_start(args, format);
    buf[BUFSZ] = 0x15;
    (void) vsnprintf(buf, BUFSZ, format, args);
    va_end(args);
    if (buf[BUFSZ] != 0x15) ABORT("MAK_printf clobbered stack");
    if (MAK_write(stdout, buf, strlen(buf)) < 0)
      ABORT("write to stdout failed");
}


MAK_INNER void MAK_warn_proc(char* msg, MAK_word arg){
    MAK_err_printf(msg, arg);
}

