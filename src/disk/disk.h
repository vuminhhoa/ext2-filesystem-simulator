#ifndef EXT2SIM_DISK_H
#define EXT2SIM_DISK_H

#include "common/common.h"

typedef struct DISK_OPERATIONS {
    int (*read_sector)(struct DISK_OPERATIONS *, SECTOR, void *);
    int (*write_sector)(struct DISK_OPERATIONS *, SECTOR, const void *);
    SECTOR numberOfSectors;
    unsigned int bytesPerSector;
    void *pdata;
} DISK_OPERATIONS;

#endif
