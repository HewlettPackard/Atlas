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

#ifndef _MAKALU_INIT_H
#define _MAKALU_INIT_H

/* determines whether we are starting for */
/* the first time, collecting offline or */
/* restarting online after a crash. */
MAK_EXTERN int MAK_run_mode;
MAK_EXTERN MAK_bool MAK_is_initialized;

#ifdef MAK_THREADS
 /* Must be called from the main thread  */
 /* for correct behavior */
 /* Relies on main's thread local variable */
 MAK_INNER void MAK_teardown_main_thread_local(void);
#endif

#endif
