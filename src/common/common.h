#ifndef EXT2SIM_COMMON_H
#define EXT2SIM_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/types.h"

#define ZeroMemory(a, b) memset((a), 0, (b))

#define PRINTF printf
#define WARNING(...) fprintf(stderr, __VA_ARGS__)

typedef DWORD SECTOR;

#define EXT2_ERROR (-1)
#define EXT2_SUCCESS 0

#endif
