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

set (EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR}/examples)

set (ATLAS_EXAMPLE_SRCS writer reader)
set (ATLAS_EXAMPLE_TGTS writer reader)

foreach (t ${ATLAS_EXAMPLE_SRCS})
    add_executable (${t} "${t}.cpp")
    target_link_libraries ("${t}" atlas pthread rt)
endforeach ()

foreach (t ${ATLAS_EXAMPLE_TGTS})
    get_target_property(TEMP_PROPERTY ${t} COMPILE_FLAGS)
    #set_target_properties(${t} PROPERTIES COMPILE_FLAGS "${NVM_INSTR_FLAGS}")
endforeach ()
