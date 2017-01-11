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
 *   gc_priv.h
 *
 *   Copyright 1988, 1989 Hans-J. Boehm, Alan J. Demers
 *   Copyright (c) 1991-1994 by Xerox Corporation.  All rights reserved.
 *   Copyright (c) 1996-1999 by Silicon Graphics.  All rights reserved.
 *   Copyright (c) 1999-2004 Hewlett-Packard Development Company, L.P.
 *
 *
 *   THIS MATERIAL IS PROVIDED AS IS, WITH ABSOLUTELY NO WARRANTY EXPRESSED
 *   OR IMPLIED.  ANY USE IS AT YOUR OWN RISK.
 *
 *   Permission is hereby granted to use or copy this program
 *   for any purpose,  provided the above notices are retained on all copies.
 *   Permission to modify the code and to distribute modified code is granted,
 *   provided the above notices are retained, and a notice that the code was
 *   modified is included with the above copyright notice.
 */

#ifndef _MAKALU_UTIL_H
#define _MAKALU_UTIL_H

#include <string.h>

#ifdef MAK_ASSERTIONS
#  define MAK_ASSERT(expr) \
                if (!(expr)) { \
                  MAK_err_printf("Assertion failure: %s:%d\n", \
                                __FILE__, __LINE__); \
                  ABORT("assertion failure"); \
                }
#else
#  define MAK_ASSERT(expr)
#endif

#ifndef MAK_ATTR_FORMAT_PRINTF
# if defined(__GNUC__) && __GNUC__ >= 3
#   define MAK_ATTR_FORMAT_PRINTF(spec_argnum, first_checked) \
        __attribute__((__format__(__printf__, spec_argnum, first_checked)))
# else
#   define MAK_ATTR_FORMAT_PRINTF(spec_argnum, first_checked)
# endif
#endif

#define BUFSZ 1024

void MAK_printf(const char * format, ...)
                        MAK_ATTR_FORMAT_PRINTF(1, 2);
                        /* A version of printf that doesn't allocate,   */
                        /* 1K total output length.                      */
                        /* (We use sprintf.  Hopefully that doesn't     */
                        /* allocate for long arguments.)                */
void MAK_err_printf(const char * format, ...)
                        MAK_ATTR_FORMAT_PRINTF(1, 2);


#if defined(__GNUC__) && __GNUC__ >= 3
# define EXPECT(expr, outcome) __builtin_expect(expr,outcome)
  /* Equivalent to (expr), but predict that usually (expr)==outcome. */
#else
# define EXPECT(expr, outcome) (expr)
#endif /* __GNUC__ */

# define MAK_STATIC_ASSERT(expr) (void)sizeof(char[(expr)? 1 : -1])

#if defined(__GNUC__) && __GNUC__ >= 3
#           define PREFETCH(x) __builtin_prefetch((x), 0, 0)
#           define PREFETCH_FOR_WRITE(x) __builtin_prefetch((x), 1)
#else
#           define PREFETCH(x)
#           define PREFETCH_FOR_WRITE(x)
#endif

/* Print warning message, e.g. almost out of memory.    */
#define WARN(msg, arg) MAK_warn_proc("MAK Warning: " msg, \
                                               (MAK_word)(arg))

MAK_INNER void MAK_warn_proc(char* msg, MAK_word arg);

#define ABORT(msg) MAK_abort(msg)
MAK_INNER void MAK_abort(const char *msg);

#define EXIT() (void)exit(1)

#define BZERO(x,n)  memset(x, 0, (size_t)(n))
#define BCOPY(x,y,n) memcpy(y, x, (size_t)(n))



#endif
