#ifndef EXT2SIM_DISKSIM_H
#define EXT2SIM_DISKSIM_H

#include "disk/disk.h"

/* The simulated disk: 256 MiB plus one block, held in memory. */
#define DISKSIM_SECTOR_SIZE 512
#define DISKSIM_DISK_SIZE (268435456u + 1024u)
#define DISKSIM_NUMBER_OF_SECTORS (DISKSIM_DISK_SIZE / DISKSIM_SECTOR_SIZE)

[[nodiscard]] int disksim_init(SECTOR numberOfSectors, unsigned int bytesPerSector, DISK_OPERATIONS *disk);
void disksim_uninit(DISK_OPERATIONS *disk);
[[nodiscard]] int disksim_read(DISK_OPERATIONS *disk, SECTOR sector, void *data);
[[nodiscard]] int disksim_write(DISK_OPERATIONS *disk, SECTOR sector, const void *data);

#endif
