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

#include "region_test.h"
#include <assert.h>
#include <stdio.h>

uint32_t rgn_id;

int main() {
    // WORK and ITERATIONS are macros defined in compilation
    NVM_Initialize();
    rgn_id = NVM_CreateRegion("createclose", O_RDWR);
    test(rgn_id);
    NVM_CloseRegion(rgn_id);
    NVM_Finalize();

    return 0;
}
