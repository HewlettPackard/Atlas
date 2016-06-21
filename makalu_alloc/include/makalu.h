#ifndef _MAKALU_H
#define _MAKALU_H

#define MAK_API 
#define MAK_CALL 
#define MAK_CALLBACK MAK_CALL

typedef unsigned long long MAK_word;
typedef long long MAK_signed_word;


typedef int (MAK_CALLBACK *MAK_persistent_memalign)(void** memptr,
              size_t alignment, size_t size);


#endif
