/* Block and inode allocation, and the free counts kept in the superblock and group descriptors. */
#include "ext2/internal.h"

#define GDT_PER_BLOCK (MAX_BLOCK_SIZE / sizeof(EXT2_GROUP_DESCRIPTOR))
static SECTOR gd_block(UINT32 group) { return GDT_BASE + group / GDT_PER_BLOCK; }

[[nodiscard]] static int read_group_descriptor(EXT2_FILESYSTEM *fs, UINT32 group, EXT2_GROUP_DESCRIPTOR *gd) {
    BYTE block[MAX_BLOCK_SIZE];

    if (block_read(fs->disk, gd_block(group), block))
        return EXT2_ERROR;
    memcpy(gd, block + (group % GDT_PER_BLOCK) * sizeof(EXT2_GROUP_DESCRIPTOR), sizeof(EXT2_GROUP_DESCRIPTOR));
    return EXT2_SUCCESS;
}

static bool add_count32(UINT32 *count, int delta) {
    INT64 value = (INT64)*count + delta;

    if (value < 0 || value > UINT32_MAX)
        return false;
    *count = (UINT32)value;
    return true;
}

static bool add_count16(UINT16 *count, int delta) {
    INT64 value = (INT64)*count + delta;

    if (value < 0 || value > UINT16_MAX)
        return false;
    *count = (UINT16)value;
    return true;
}

/*
 * Moves the free counts of the superblock and of one group descriptor together, so they never disagree
 * with each other. Only the primary copies (group 0) are updated, as in ext2.
 */
int adjust_counts(EXT2_FILESYSTEM *fs, UINT32 group, int blocks, int inodes, int directories) {
    BYTE block[MAX_BLOCK_SIZE];
    EXT2_SUPER_BLOCK *sb = (EXT2_SUPER_BLOCK *)block;
    EXT2_GROUP_DESCRIPTOR *gd;

    if (block_read(fs->disk, SUPER_BLOCK_BASE, block))
        return EXT2_ERROR;
    if (!add_count32(&sb->free_block_count, blocks) || !add_count32(&sb->free_inode_count, inodes))
        return EXT2_ERROR; /* a count would go negative: the metadata is already inconsistent */
    if (block_write(fs->disk, SUPER_BLOCK_BASE, block))
        return EXT2_ERROR;
    fs->sb.free_block_count = sb->free_block_count;
    fs->sb.free_inode_count = sb->free_inode_count;

    if (block_read(fs->disk, gd_block(group), block))
        return EXT2_ERROR;
    gd = (EXT2_GROUP_DESCRIPTOR *)block + group % GDT_PER_BLOCK;
    if (!add_count16(&gd->free_blocks_count, blocks) || !add_count16(&gd->free_inodes_count, inodes) ||
        !add_count16(&gd->directories_count, directories))
        return EXT2_ERROR;
    return block_write(fs->disk, gd_block(group), block);
}

/* Finds a free data block in any group, marks it used and zeroes it. */
int alloc_block(EXT2_FILESYSTEM *fs, UINT32 *blockNumber) {
    BYTE bitmap[MAX_BLOCK_SIZE];
    BYTE zero[MAX_BLOCK_SIZE] = {0};
    EXT2_GROUP_DESCRIPTOR gd;
    UINT32 group, bit, limit;

    for (group = 0; group < NUMBER_OF_GROUPS; group++) {
        if (read_group_descriptor(fs, group, &gd) || gd.free_blocks_count == 0)
            continue;
        if (block_read(fs->disk, group_base(group) + BLOCK_BITMAP_BASE, bitmap))
            return EXT2_ERROR;

        limit = MIN(BLOCKS_PER_GROUP, NUMBER_OF_BLOCK - group_base(group));
        for (bit = DATA_BLOCK_BASE; bit < limit; bit++) {
            if (test_bit(bitmap, bit))
                continue;
            set_bit(bitmap, bit);
            if (block_write(fs->disk, group_base(group) + BLOCK_BITMAP_BASE, bitmap) ||
                adjust_counts(fs, group, -1, 0, 0) || block_write(fs->disk, group_base(group) + bit, zero))
                return EXT2_ERROR;
            *blockNumber = group_base(group) + bit;
            return EXT2_SUCCESS;
        }
    }
    return EXT2_ERROR;
}

int free_block(EXT2_FILESYSTEM *fs, UINT32 blockNumber) {
    BYTE bitmap[MAX_BLOCK_SIZE];
    UINT32 group = GET_DATA_GROUP(blockNumber);
    UINT32 bit = blockNumber % BLOCKS_PER_GROUP;

    if (group >= NUMBER_OF_GROUPS || bit < DATA_BLOCK_BASE)
        return EXT2_ERROR;
    if (block_read(fs->disk, group_base(group) + BLOCK_BITMAP_BASE, bitmap))
        return EXT2_ERROR;
    if (!test_bit(bitmap, bit))
        return EXT2_ERROR; /* already free: refuse instead of flipping it back to used */
    clear_bit(bitmap, bit);
    if (block_write(fs->disk, group_base(group) + BLOCK_BITMAP_BASE, bitmap))
        return EXT2_ERROR;
    return adjust_counts(fs, group, 1, 0, 0);
}

int alloc_inode(EXT2_FILESYSTEM *fs, UINT32 *inodeNumber) {
    BYTE bitmap[MAX_BLOCK_SIZE];
    EXT2_GROUP_DESCRIPTOR gd;
    UINT32 group, bit;

    for (group = 0; group < NUMBER_OF_GROUPS; group++) {
        if (read_group_descriptor(fs, group, &gd) || gd.free_inodes_count == 0)
            continue;
        if (block_read(fs->disk, group_base(group) + INODE_BITMAP_BASE, bitmap))
            return EXT2_ERROR;

        for (bit = 0; bit < INODES_PER_GROUP; bit++) {
            if (test_bit(bitmap, bit))
                continue;
            set_bit(bitmap, bit);
            if (block_write(fs->disk, group_base(group) + INODE_BITMAP_BASE, bitmap) ||
                adjust_counts(fs, group, 0, -1, 0))
                return EXT2_ERROR;
            *inodeNumber = group * INODES_PER_GROUP + bit + 1;
            return EXT2_SUCCESS;
        }
    }
    return EXT2_ERROR;
}

int free_inode(EXT2_FILESYSTEM *fs, UINT32 inodeNumber) {
    BYTE bitmap[MAX_BLOCK_SIZE];
    UINT32 group = GET_INODE_GROUP(inodeNumber);
    UINT32 bit = (inodeNumber - 1) % INODES_PER_GROUP;

    if (inodeNumber < FIRST_NON_RESERVED_INODE || group >= NUMBER_OF_GROUPS)
        return EXT2_ERROR;
    if (block_read(fs->disk, group_base(group) + INODE_BITMAP_BASE, bitmap))
        return EXT2_ERROR;
    if (!test_bit(bitmap, bit))
        return EXT2_ERROR;
    clear_bit(bitmap, bit);
    if (block_write(fs->disk, group_base(group) + INODE_BITMAP_BASE, bitmap))
        return EXT2_ERROR;
    return adjust_counts(fs, group, 0, 1, 0);
}
