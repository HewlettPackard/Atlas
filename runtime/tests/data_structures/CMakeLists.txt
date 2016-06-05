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

set (ATLAS_DATA_STRUCT_SRCS cow_array_list queue_orig sll stores alarm_clock)
set (ATLAS_DATA_STRUCT_NVM_TGTS alarm_clock_nvm queue_nvm stores_nvm cow_array_list_nvm sll_nvm)
set (ATLAS_DATA_STRUCT_TGTS ${ATLAS_DATA_STRUCT_NVM_TGTS} alarm_clock queue_orig stores cow_array_list sll sll_ll sll_mt sll_mt_ll)

foreach (t ${ATLAS_DATA_STRUCT_SRCS})
    if (${t} STREQUAL "sll")
        add_executable (${t} "${t}.cpp")
        add_executable ("${t}_mt" "${t}_mt.cpp")
        add_executable ("${t}_ll" "${t}_ll.cpp")
        add_executable ("${t}_mt_ll" "${t}_mt_ll.cpp")
        #add_executable ("${t}_mt_nvm" "${t}_mt_nvm.cpp")
        add_executable ("${t}_nvm" "${t}_nvm.cpp")
        target_link_libraries ("${t}_mt" atlas pthread rt)
        target_link_libraries ("${t}_ll" atlas pthread rt)
        target_link_libraries ("${t}_mt_ll" atlas pthread rt)
        #target_link_libraries ("${t}_mt_nvm" atlas pthread rt)
        target_link_libraries ("${t}_nvm" atlas pthread rt)
    elseif (${t} STREQUAL "queue_orig")
        add_executable (${t} "${t}.c")
        add_executable (queue_nvm "queue_nvm.c")
        target_link_libraries ("queue_nvm" atlas pthread rt)
    else ()
        add_executable (${t} "${t}.c")
        add_executable ("${t}_nvm" "${t}_nvm.c")
        target_link_libraries ("${t}_nvm" atlas pthread rt)
    endif ()
    target_link_libraries (${t} pthread rt)
endforeach ()

foreach (t ${ATLAS_DATA_STRUCT_TGTS})
    set_target_properties(${t} PROPERTIES COMPILE_FLAGS "${APP_FLAGS}")
endforeach ()

foreach (t ${ATLAS_DATA_STRUCT_NVM_TGTS})
    get_target_property(TEMP_PROPERTY ${t} COMPILE_FLAGS)
    set_target_properties(${t} PROPERTIES COMPILE_FLAGS "${NVM_INSTR_FLAGS} ${TEMP_PROPERTY}")
endforeach ()
