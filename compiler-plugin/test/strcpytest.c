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
 

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include "atlas_api.h"
#include "atlas_alloc.h"





int main(){
  NVM_Initialize();
  uint32_t rgn_id = NVM_FindOrCreateRegion("strcpy", O_RDWR, NULL);

  char *str1 = nvm_alloc(30*sizeof(char), rgn_id);
  char *str2 = nvm_alloc(30*sizeof(char), rgn_id);

  char strdummy[] = "test string banter";
  strcpy(str1, strdummy); 
  strcpy(str2, str1);
  strncpy(str2, str1, 5);
  strcat(str2, str1);
  strncat(str2, str1, 5);
  //NVM_STRCPY(str1, strdummy);
  //NVM_STRCPY(str2, str1);
  //NVM_STRNCPY(str1, str2, 5);
  //NVM_STRCAT(str2, str1);
  //NVM_STRNCAT(str1, str2, 5);
  //NVM_PrintStats();

  NVM_Finalize();
}
