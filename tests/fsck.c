#include "fsck.h"

/* Bits set in the first `bits` bits of a bitmap; `bits` is a multiple of 8. */
static UINT32 count_bits(const BYTE *bitmap, UINT32 bits) {
    static BYTE ones[256];
    static bool ready;
    UINT32 i, count = 0;

    if (!ready) {
        for (i = 1; i < 256; i++)
            ones[i] = (BYTE)(ones[i / 2] + (i & 1u));
        ready = true;
    }
    for (i = 0; i < bits / 8; i++)
        count += ones[bitmap[i]];
    return count;
}

bool fsck_counts_match(DISK_OPERATIONS *disk, bool verbose) {
    BYTE block[MAX_BLOCK_SIZE];
    BYTE gdt[MAX_BLOCK_SIZE];
    EXT2_SUPER_BLOCK sb;
    UINT32 group, freeBlocks = 0, freeInodes = 0;
    bool ok = true;

    if (block_read(disk, SUPER_BLOCK_BASE, &sb) || block_read(disk, GDT_BASE, gdt))
        return false;

    for (group = 0; group < NUMBER_OF_GROUPS; group++) {
        const EXT2_GROUP_DESCRIPTOR *gd = (const EXT2_GROUP_DESCRIPTOR *)gdt + group;
        UINT32 base = group * BLOCKS_PER_GROUP;
        UINT32 bitmapFreeBlocks, bitmapFreeInodes;

        if (block_read(disk, base + BLOCK_BITMAP_BASE, block))
            return false;
        bitmapFreeBlocks = BLOCKS_PER_GROUP - count_bits(block, BLOCKS_PER_GROUP);
        if (block_read(disk, base + INODE_BITMAP_BASE, block))
            return false;
        bitmapFreeInodes = INODES_PER_GROUP - count_bits(block, INODES_PER_GROUP);

        if (gd->free_blocks_count != bitmapFreeBlocks || gd->free_inodes_count != bitmapFreeInodes) {
            if (verbose)
                fprintf(stderr, "group %u: descriptor says %u free blocks / %u free inodes, bitmaps say %u / %u\n",
                        group, gd->free_blocks_count, gd->free_inodes_count, bitmapFreeBlocks, bitmapFreeInodes);
            ok = false;
        }
        freeBlocks += gd->free_blocks_count;
        freeInodes += gd->free_inodes_count;
    }

    if (sb.free_block_count != freeBlocks || sb.free_inode_count != freeInodes) {
        if (verbose)
            fprintf(stderr, "superblock says %u / %u free, groups add up to %u / %u\n", sb.free_block_count,
                    sb.free_inode_count, freeBlocks, freeInodes);
        ok = false;
    }
    return ok;
}

UINT32 fsck_free_blocks(DISK_OPERATIONS *disk) {
    EXT2_SUPER_BLOCK sb;

    if (block_read(disk, SUPER_BLOCK_BASE, &sb))
        return UINT32_MAX;
    return sb.free_block_count;
}
