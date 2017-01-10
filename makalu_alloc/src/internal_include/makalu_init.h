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
