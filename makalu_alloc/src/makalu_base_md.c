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

#include "makalu_internal.h"


MAK_INNER word MAK_page_size = SYS_PAGESIZE;
MAK_INNER struct _MAK_base_md* MAK_base_md_ptr = NULL;
MAK_INNER struct obj_kind* MAK_obj_kinds = NULL;
MAK_INNER int MAK_run_mode = STARTING_ONLINE;




