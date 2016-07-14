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

#include "atlas_alloc.h"
#include "atlas_api.h"

#include <stdio.h>
#include <stdlib.h>

uint32_t rgn_id;

int main() {
    NVM_Initialize();
    rgn_id = NVM_FindOrCreateRegion("free_test", O_RDWR, NULL);

    int *testint;
    void *rgn_root = NVM_GetRegionRoot(rgn_id);
    if (!rgn_root) {
        testint = (int *)nvm_alloc(sizeof(int), rgn_id);
        *testint = 10;
        NVM_SetRegionRoot(rgn_id, testint);
    } else {
        testint = (int *)rgn_root;
        printf("testint is %i\n", *testint);
    }

#ifdef CONSISTENCY_FAIL
    exit(0);
#endif

    nvm_free(testint);

    NVM_DeleteRegion("free_test");
    NVM_Finalize();

    return 0;
}
