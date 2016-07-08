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
 */

// This is an example of a program that has been manually instrumented
// with Atlas internal APIs. This is just for understanding
// purposes. It is recommended that users use an Atlas-aware compiler
// (such as the one provided through the LLVM compiler-plugin under
// the root Atlas directory) to perform this instrumentation. Manual
// instrumentation is burdensome and error-prone and probably has very
// little performance advantage. If you still want to instrument
// manually, make sure you compile the manually instrumented program
// using your favorite compiler, not the Atlas-aware compiler.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atlas_alloc.h"
#include "atlas_api.h"

#define MAXLEN 256

int main() {
    NVM_Initialize();
    uint32_t rgn_id = NVM_FindOrCreateRegion("memtest", O_RDWR, NULL);

    char *buf1 = (char *)nvm_alloc(MAXLEN, rgn_id);
    char *buf2 = (char *)nvm_alloc(MAXLEN, rgn_id);

    strcpy(buf1, "This is a test                                    \n");

    NVM_BEGIN_DURABLE();

    NVM_MEMSET(buf1 + 15, ':', 1);
    NVM_MEMCPY(buf2, buf1, MAXLEN);
    NVM_MEMMOVE(buf2 + 20, buf2, MAXLEN / 2);

    NVM_END_DURABLE();

    fprintf(stderr, "buf1=%s\n", buf1);
    fprintf(stderr, "buf2=%s\n", buf2);

    NVM_CloseRegion(rgn_id);
    NVM_Finalize();

    return 0;
}
