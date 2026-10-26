#ifndef EXT2SIM_EXT2_INTERNAL_H
#define EXT2SIM_EXT2_INTERNAL_H

/* Shared between the modules of src/ext2; not part of the public API. */

#include "ext2/ext2.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

extern const char DOT_NAME[MAX_ENTRY_NAME_LENGTH];
extern const char DOTDOT_NAME[MAX_ENTRY_NAME_LENGTH];

static inline UINT32 group_base(UINT32 group) { return group * BLOCKS_PER_GROUP; }

/* Bitmaps number bits from the most significant bit of each byte. */
static inline bool test_bit(const BYTE *bitmap, UINT32 number) {
    return ((UINT32)bitmap[number / 8] >> (7u - number % 8)) & 1u;
}

static inline void set_bit(BYTE *bitmap, UINT32 number) { bitmap[number / 8] |= (BYTE)(0x80u >> (number % 8)); }

static inline void clear_bit(BYTE *bitmap, UINT32 number) { bitmap[number / 8] &= (BYTE) ~(0x80u >> (number % 8)); }

static inline bool is_directory(const INODE *inode) { return (inode->mode & FILE_TYPE_MASK) == FILE_TYPE_DIR; }

/* alloc.c: bitmaps and the free counts in the superblock and group descriptors */
[[nodiscard]] int adjust_counts(EXT2_FILESYSTEM *fs, UINT32 group, int blocks, int inodes, int directories);
[[nodiscard]] int alloc_block(EXT2_FILESYSTEM *fs, UINT32 *blockNumber);
[[nodiscard]] int free_block(EXT2_FILESYSTEM *fs, UINT32 blockNumber);
[[nodiscard]] int alloc_inode(EXT2_FILESYSTEM *fs, UINT32 *inodeNumber);
[[nodiscard]] int free_inode(EXT2_FILESYSTEM *fs, UINT32 inodeNumber);

/* inode.c */
[[nodiscard]] int put_inode(EXT2_FILESYSTEM *fs, UINT32 inode_num, const INODE *inodeBuffer);
[[nodiscard]] int release_blocks(EXT2_FILESYSTEM *fs, const INODE *inode);

/* dir.c */
bool is_dot_entry(const EXT2_DIR_ENTRY *entry);
void fill_dot_entries(BYTE block[MAX_BLOCK_SIZE], UINT32 self, UINT32 parent);
[[nodiscard]] int set_entry(EXT2_FILESYSTEM *fs, const EXT2_DIR_ENTRY_LOCATION *location, const EXT2_DIR_ENTRY *value);
[[nodiscard]] int lookup_entry(EXT2_FILESYSTEM *fs, UINT32 inode, const char *name, EXT2_NODE *retEntry);
[[nodiscard]] int insert_entry(EXT2_NODE *parent, const EXT2_DIR_ENTRY *entry, EXT2_DIR_ENTRY_LOCATION *location);

/* name.c */
UINT32 name_length(const char name[MAX_ENTRY_NAME_LENGTH]);

#endif
