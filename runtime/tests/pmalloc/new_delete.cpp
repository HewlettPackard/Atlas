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
#include "atlas_alloc_cpp.hpp"
#include "atlas_api.h"

struct A
{
    A() {}
    ~A() {}
};

int main()
{
    NVM_Initialize();
    uint32_t a_rgn_id = NVM_FindOrCreateRegion("A", O_RDWR, NULL);

    A *nvm_obj = new (NVM_GetRegion(a_rgn_id)) A;
    A *transient_obj = new A;

    A *nvm_arr = new (NVM_GetRegion(a_rgn_id)) A[20];
    A *transient_arr = new A[10];
    
    NVM_Destroy(nvm_obj);
    NVM_Destroy(transient_obj); // equivalent to delete transient_obj

    NVM_Destroy_Array(nvm_arr);
    NVM_Destroy_Array(transient_arr); // equivalent to delete [] transient_arr

    NVM_CloseRegion(a_rgn_id);
    NVM_Finalize();
    
}

