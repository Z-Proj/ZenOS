#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp  memcmp
#define SSFN_memset  memset
#define SSFN_memcpy  memcpy
#define SSFN_realloc realloc
#define SSFN_free    free
#include "ssfn.h"

#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_IMPLEMENTATION
#include "nuklear.h"
