/* Reading and writing inodes in the per-group inode tables. */
#include "ext2/internal.h"

[[nodiscard]] static int inode_location(UINT32 inodeNumber, SECTOR *block, UINT32 *offset) {
    UINT32 index, group;

    if (inodeNumber == 0 || inodeNumber > NUMBER_OF_INODES)
        return EXT2_ERROR;
    index = inodeNumber - 1;
    group = index / INODES_PER_GROUP;
    *block = group_base(group) + INODE_TABLE_BASE + (index % INODES_PER_GROUP) / INODES_PER_BLOCK;
    *offset = (index % INODES_PER_GROUP) % INODES_PER_BLOCK * INODE_SIZE;
    return EXT2_SUCCESS;
}

int get_inode(EXT2_FILESYSTEM *fs, UINT32 inode_num, INODE *inodeBuffer) {
    BYTE block[MAX_BLOCK_SIZE];
    SECTOR blockNumber;
    UINT32 offset;

    if (inode_location(inode_num, &blockNumber, &offset) || block_read(fs->disk, blockNumber, block))
        return EXT2_ERROR;
    memcpy(inodeBuffer, block + offset, sizeof(INODE));
    return EXT2_SUCCESS;
}

int put_inode(EXT2_FILESYSTEM *fs, UINT32 inode_num, const INODE *inodeBuffer) {
    BYTE block[MAX_BLOCK_SIZE];
    SECTOR blockNumber;
    UINT32 offset;

    if (inode_location(inode_num, &blockNumber, &offset) || block_read(fs->disk, blockNumber, block))
        return EXT2_ERROR;
    memcpy(block + offset, inodeBuffer, sizeof(INODE));
    return block_write(fs->disk, blockNumber, block);
}
int release_blocks(EXT2_FILESYSTEM *fs, const INODE *inode) {
    UINT32 i;

    for (i = 0; i < inode->blocks && i < EXT2_D_BLOCKS; i++)
        if (free_block(fs, inode->block[i]))
            return EXT2_ERROR;
    return EXT2_SUCCESS;
}
