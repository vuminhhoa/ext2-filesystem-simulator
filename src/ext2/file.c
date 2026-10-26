/* Reading and writing file contents. */
#include "ext2/internal.h"

long ext2_read(EXT2_NODE *file, unsigned long offset, unsigned long length, char *buffer) {
    BYTE block[MAX_BLOCK_SIZE];
    INODE inode;
    unsigned long done = 0;

    if (get_inode(file->fs, file->entry.inode, &inode) || is_directory(&inode))
        return EXT2_ERROR;
    if (offset >= inode.size)
        return 0;
    length = MIN(length, inode.size - offset);

    while (done < length) {
        unsigned long position = offset + done;
        unsigned long inBlock = position % MAX_BLOCK_SIZE;
        unsigned long chunk = MIN(length - done, MAX_BLOCK_SIZE - inBlock);

        if (block_read(file->fs->disk, inode.block[position / MAX_BLOCK_SIZE], block))
            return EXT2_ERROR;
        memcpy(buffer + done, block + inBlock, chunk);
        done += chunk;
    }
    return (long)done;
}

/* Only direct blocks are supported, so a file holds at most EXT2_D_BLOCKS blocks (12 KiB). */
long ext2_write(EXT2_NODE *file, unsigned long offset, unsigned long length, const char *buffer) {
    EXT2_FILESYSTEM *fs = file->fs;
    BYTE block[MAX_BLOCK_SIZE];
    INODE inode;
    unsigned long end = offset + length, done = 0;
    UINT32 needed;

    if (get_inode(fs, file->entry.inode, &inode) || is_directory(&inode))
        return EXT2_ERROR;
    if (end < offset || end > (unsigned long)EXT2_D_BLOCKS * MAX_BLOCK_SIZE)
        return EXT2_ERROR;

    needed = (UINT32)((MAX(end, inode.size) + MAX_BLOCK_SIZE - 1) / MAX_BLOCK_SIZE);
    while (inode.blocks < needed) {
        if (alloc_block(fs, &inode.block[inode.blocks])) {
            /* Keep the blocks already allocated attached to the file, so none leak. */
            (void)put_inode(fs, file->entry.inode, &inode);
            return EXT2_ERROR;
        }
        inode.blocks++;
    }

    while (done < length) {
        unsigned long position = offset + done;
        unsigned long inBlock = position % MAX_BLOCK_SIZE;
        unsigned long chunk = MIN(length - done, MAX_BLOCK_SIZE - inBlock);
        UINT32 target = inode.block[position / MAX_BLOCK_SIZE];

        if (block_read(fs->disk, target, block))
            return EXT2_ERROR;
        memcpy(block + inBlock, buffer + done, chunk);
        if (block_write(fs->disk, target, block))
            return EXT2_ERROR;
        done += chunk;
    }

    inode.size = (UINT32)MAX(end, inode.size);
    if (put_inode(fs, file->entry.inode, &inode))
        return EXT2_ERROR;
    return (long)done;
}
