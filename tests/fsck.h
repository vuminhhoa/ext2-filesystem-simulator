#ifndef EXT2SIM_TESTS_FSCK_H
#define EXT2SIM_TESTS_FSCK_H

#include "ext2/ext2.h"

/*
 * Recounts every group's bitmaps and compares them with the group descriptors and the superblock, like
 * e2fsck's pass 5. Prints what disagrees to stderr when `verbose` is set.
 */
[[nodiscard]] bool fsck_counts_match(DISK_OPERATIONS *disk, bool verbose);

/* The superblock's free block count, or UINT32_MAX if it cannot be read. */
[[nodiscard]] UINT32 fsck_free_blocks(DISK_OPERATIONS *disk);

#endif
