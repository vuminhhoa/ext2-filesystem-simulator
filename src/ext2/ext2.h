#ifndef EXT2SIM_EXT2_H
#define EXT2SIM_EXT2_H

/* Public API of the file system, used by the shell and the tests. */

#include "disk/disk.h"
#include "ext2/layout.h"

typedef struct {
    EXT2_SUPER_BLOCK sb;
    DISK_OPERATIONS *disk;
} EXT2_FILESYSTEM;

typedef struct {
    UINT32 group;
    UINT32 block;
    UINT32 offset;
} EXT2_DIR_ENTRY_LOCATION;

typedef struct {
    EXT2_FILESYSTEM *fs;
    EXT2_DIR_ENTRY entry;
    EXT2_DIR_ENTRY_LOCATION location;
} EXT2_NODE;

typedef int (*EXT2_NODE_ADD)(void *, EXT2_NODE *);

[[nodiscard]] int ext2_format(DISK_OPERATIONS *disk);
[[nodiscard]] int read_superblock(EXT2_FILESYSTEM *fs, EXT2_NODE *root);

[[nodiscard]] int ext2_create(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry);
[[nodiscard]] int ext2_mkdir(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry);
[[nodiscard]] int ext2_lookup(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry);
[[nodiscard]] int ext2_read_dir(EXT2_NODE *dir, EXT2_NODE_ADD adder, void *list);
[[nodiscard]] int ext2_remove(EXT2_NODE *parent, EXT2_NODE *rmfile);
[[nodiscard]] int ext2_rmdir(EXT2_NODE *parent, EXT2_NODE *rmdir);

/* Return the number of bytes read or written, or EXT2_ERROR. */
[[nodiscard]] long ext2_read(EXT2_NODE *file, unsigned long offset, unsigned long length, char *buffer);
[[nodiscard]] long ext2_write(EXT2_NODE *file, unsigned long offset, unsigned long length, const char *buffer);

[[nodiscard]] int get_inode(EXT2_FILESYSTEM *fs, UINT32 inode_num, INODE *inodeBuffer);
[[nodiscard]] int format_name(const char *name, char formatted[MAX_ENTRY_NAME_LENGTH]);

[[nodiscard]] int block_read(DISK_OPERATIONS *disk, SECTOR block, void *data);
[[nodiscard]] int block_write(DISK_OPERATIONS *disk, SECTOR block, const void *data);

#endif
