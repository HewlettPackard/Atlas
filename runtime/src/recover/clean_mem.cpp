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
 

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "atlas_alloc.h"
#include "util.hpp"
#include "pregion_configs.hpp"
#include "pregion_mgr.hpp"

using namespace Atlas;

int main(int argc, char **argv)
{
    char input[kMaxlen_];
    
    assert(argc < 3);

    if (argc == 1)
    {
        fprintf(stderr,
                "Are you sure you want to delete all regions (yes/no)? ");
        while (true)
        {
            char *s = fgets(input, kMaxlen_, stdin);
            if (!strcmp(s, "yes\n")) break;
            else if (!strcmp(s, "no\n")) return 0;
            else fprintf(stderr, "Please enter yes or no: ");
        }
    }
    else if (strcmp(argv[1], "-f"))
    {
        fprintf(stderr, "Unknown option: use clean_mem -f\n");
        return 0;
    }

    PRegionMgr::createInstance();

    PRegionMgr::getInstance().deleteForcefullyAllPRegions();

    PRegionMgr::deleteInstance();
    
    char *rgn_tab = NVM_GetRegionTablePath();
    unlink(rgn_tab);
    free(rgn_tab);
    fprintf(stderr, "All region data/metadata removed successfully\n");
    return 0;
}

