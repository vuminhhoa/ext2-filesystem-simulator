#include "disk/disksim.h"

typedef struct {
    BYTE *address;
} DISK_MEMORY;

int disksim_init(SECTOR numberOfSectors, unsigned int bytesPerSector, DISK_OPERATIONS *disk) {
    DISK_MEMORY *memory;

    if (disk == nullptr)
        return -1;
    ZeroMemory(disk, sizeof(DISK_OPERATIONS));

    memory = malloc(sizeof(DISK_MEMORY));
    if (memory == nullptr)
        return -1;

    memory->address = calloc(numberOfSectors, bytesPerSector);
    if (memory->address == nullptr) {
        free(memory);
        return -1;
    }

    disk->pdata = memory;
    disk->read_sector = disksim_read;
    disk->write_sector = disksim_write;
    disk->numberOfSectors = numberOfSectors;
    disk->bytesPerSector = bytesPerSector;
    return 0;
}

void disksim_uninit(DISK_OPERATIONS *disk) {
    DISK_MEMORY *memory;

    if (disk == nullptr || disk->pdata == nullptr)
        return;

    memory = disk->pdata;
    free(memory->address);
    free(memory);
    disk->pdata = nullptr;
}

int disksim_read(DISK_OPERATIONS *disk, SECTOR sector, void *data) {
    DISK_MEMORY *memory = disk->pdata;

    if (sector >= disk->numberOfSectors)
        return -1;
    memcpy(data, &memory->address[(size_t)sector * disk->bytesPerSector], disk->bytesPerSector);
    return 0;
}

int disksim_write(DISK_OPERATIONS *disk, SECTOR sector, const void *data) {
    DISK_MEMORY *memory = disk->pdata;

    if (sector >= disk->numberOfSectors)
        return -1;
    memcpy(&memory->address[(size_t)sector * disk->bytesPerSector], data, disk->bytesPerSector);
    return 0;
}
