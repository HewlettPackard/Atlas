#
# (c) Copyright 2016 Hewlett Packard Enterprise Development LP
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version. This program is
# distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
# for more details. You should have received a copy of the GNU Lesser
# General Public License along with this program. If not, see
# <http://www.gnu.org/licenses/>.
#
# data_structures CMakeLists

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/tests/data_structures)

set (DS_ATLAS_TGTS alarm_clock_nvm queue_nvm stores_nvm cow_array_list_nvm sll_nvm sll_mt_ll sll_ll)
set (DS_NOATLAS_TGTS cow_array_list queue sll sll_mt stores alarm_clock)
set (DS_ALL_TGTS ${DS_ATLAS_TGTS} ${DS_NOATLAS_TGTS})

foreach (t ${DS_ALL_TGTS})
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/${t}.c")
        add_executable (${t} "${t}.c")
    else ()
        add_executable (${t} "${t}.cpp")
    endif ()
endforeach()

foreach (t ${DS_ALL_TGTS})
    set_target_properties(${t} PROPERTIES COMPILE_FLAGS "${APP_FLAGS}")
    target_link_libraries (${t} pthread rt)
endforeach ()

foreach (t ${DS_ATLAS_TGTS})
    get_target_property (TEMP_PROPERTY ${t} COMPILE_FLAGS)
    set_target_properties (${t} PROPERTIES COMPILE_FLAGS "${NVM_INSTR_FLAGS} ${TEMP_PROPERTY}")
    target_link_libraries (${t} atlas pthread rt)
endforeach ()
