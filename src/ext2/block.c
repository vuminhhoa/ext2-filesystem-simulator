/* Block I/O: a block is SECTORS_PER_BLOCK consecutive sectors of the disk. */
#include "ext2/ext2.h"

int block_write(DISK_OPERATIONS *disk, SECTOR block, const void *data) {
    const BYTE *bytes = data;
    SECTOR first = block * SECTORS_PER_BLOCK;
    SECTOR i;

    for (i = 0; i < SECTORS_PER_BLOCK; i++)
        if (disk->write_sector(disk, first + i, bytes + i * MAX_SECTOR_SIZE))
            return EXT2_ERROR;
    return EXT2_SUCCESS;
}

int block_read(DISK_OPERATIONS *disk, SECTOR block, void *data) {
    BYTE *bytes = data;
    SECTOR first = block * SECTORS_PER_BLOCK;
    SECTOR i;

    for (i = 0; i < SECTORS_PER_BLOCK; i++)
        if (disk->read_sector(disk, first + i, bytes + i * MAX_SECTOR_SIZE))
            return EXT2_ERROR;
    return EXT2_SUCCESS;
}
