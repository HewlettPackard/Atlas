#ifndef _MAKALU_H
#define _MAKALU_H

#define MAK_API 
#define MAK_CALL 
#define MAK_CALLBACK MAK_CALL


typedef int (MAK_CALLBACK *MAK_persistent_memalign)(void** memptr,
              size_t alignment, size_t size);


#endif
