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
# logger CMakeLists
set (LOGGER_SRC
     log_mgr_api.cpp
     log_mgr.cpp
     log_entry_create.cpp
     log_entry_publish.cpp
     circular_buffer.cpp
     happens_before.cpp
     log_elision.cpp)
add_library (Logger OBJECT ${LOGGER_SRC})
