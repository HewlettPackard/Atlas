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
 

#ifndef FAIL
#define FAIL

#include <cstdlib>
#include <iostream>
#include <execinfo.h>
#include <atomic>
#define FAIL_MAX 100
static std::atomic<int> fail_chance(0);
inline void fail_program () {
    if (rand() % FAIL_MAX <= fail_chance.load()){
        void *array[10];
        size_t size;
        char **strings;
        size_t i;

        size = backtrace (array, 10);
        strings = backtrace_symbols (array, size);

        for (i = 0; i < size; i++)
            std::cout << strings[i] << "\n";
        delete (strings);
        std::exit(0);
    } else {
        ++fail_chance;
    }
}
#endif
