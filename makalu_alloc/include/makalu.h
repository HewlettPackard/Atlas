#ifndef _MAKALU_H
#define _MAKALU_H

#define MAK_API 
#define MAK_CALL 
#define MAK_CALLBACK MAK_CALL

#include <stddef.h>

typedef unsigned long long MAK_word;
typedef long long MAK_signed_word;


typedef int (MAK_CALLBACK *MAK_persistent_memalign)(void** memptr,
              size_t alignment, size_t size);

MAK_API void* MAK_CALL MAK_start(MAK_persistent_memalign funcptr);
MAK_API void MAK_CALL MAK_restart(char* start_addr, MAK_persistent_memalign funcptr);
MAK_API void MAK_CALL MAK_start_off(char* start_addr,
              MAK_persistent_memalign funcptr);
MAK_API void MAK_CALL MAK_close(void);

#endif
