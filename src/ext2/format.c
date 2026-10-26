/* Formatting a disk and mounting it. */
#include "ext2/internal.h"

static UINT32 group_free_blocks(UINT32 group) {
    return MIN(BLOCKS_PER_GROUP, NUMBER_OF_BLOCK - group_base(group)) - DATA_BLOCK_BASE;
}

static UINT32 group_free_inodes(UINT32 group) {
    return group == 0 ? INODES_PER_GROUP - RESERVED_INODE_NUM : INODES_PER_GROUP;
}

static void fill_super_block(EXT2_SUPER_BLOCK *sb) {
    UINT32 group;

    ZeroMemory(sb, sizeof(EXT2_SUPER_BLOCK));
    sb->max_inode_count = NUMBER_OF_INODES;
    sb->block_count = NUMBER_OF_BLOCK;
    sb->reserved_block_count = sb->block_count / 100 * 5;
    for (group = 0; group < NUMBER_OF_GROUPS; group++) {
        sb->free_block_count += group_free_blocks(group);
        sb->free_inode_count += group_free_inodes(group);
    }
    sb->first_data_block = SUPER_BLOCK_BASE;
    sb->block_per_group = BLOCKS_PER_GROUP;
    sb->inode_per_group = INODES_PER_GROUP;
    sb->magic_signature = EXT2_MAGIC;
    sb->first_non_reserved_inode = FIRST_NON_RESERVED_INODE;
    sb->inode_structure_size = INODE_SIZE;
    sb->first_data_block_each_group = DATA_BLOCK_BASE;
}

static void fill_descriptor_table(BYTE gdt[BLOCK_NUM_GDT_PER_GROUP * MAX_BLOCK_SIZE]) {
    EXT2_GROUP_DESCRIPTOR *gd = (EXT2_GROUP_DESCRIPTOR *)gdt;
    UINT32 group;

    ZeroMemory(gdt, BLOCK_NUM_GDT_PER_GROUP * MAX_BLOCK_SIZE);
    for (group = 0; group < NUMBER_OF_GROUPS; group++) {
        gd[group].start_block_of_block_bitmap = group_base(group) + BLOCK_BITMAP_BASE;
        gd[group].start_block_of_inode_bitmap = group_base(group) + INODE_BITMAP_BASE;
        gd[group].start_block_of_inode_table = group_base(group) + INODE_TABLE_BASE;
        gd[group].free_blocks_count = (UINT16)group_free_blocks(group);
        gd[group].free_inodes_count = (UINT16)group_free_inodes(group);
    }
}

/* Every group starts with a copy of the superblock and the descriptor table, then its bitmaps and inode table. */
[[nodiscard]] static int init_group(DISK_OPERATIONS *disk, UINT32 group, EXT2_SUPER_BLOCK *sb, const BYTE *gdt) {
    BYTE block[MAX_BLOCK_SIZE];
    UINT32 i, base = group_base(group);

    sb->block_group_number = (UINT16)group;
    if (block_write(disk, base + SUPER_BLOCK_BASE, sb))
        return EXT2_ERROR;
    for (i = 0; i < BLOCK_NUM_GDT_PER_GROUP; i++)
        if (block_write(disk, base + GDT_BASE + i, gdt + i * MAX_BLOCK_SIZE))
            return EXT2_ERROR;

    ZeroMemory(block, sizeof(block));
    for (i = 0; i < DATA_BLOCK_BASE; i++)
        set_bit(block, i);
    if (block_write(disk, base + BLOCK_BITMAP_BASE, block))
        return EXT2_ERROR;

    ZeroMemory(block, sizeof(block));
    for (i = 0; group == 0 && i < RESERVED_INODE_NUM; i++)
        set_bit(block, i);
    if (block_write(disk, base + INODE_BITMAP_BASE, block))
        return EXT2_ERROR;

    ZeroMemory(block, sizeof(block));
    for (i = 0; i < BLOCK_NUM_IT_PER_GROUP; i++)
        if (block_write(disk, base + INODE_TABLE_BASE + i, block))
            return EXT2_ERROR;
    return EXT2_SUCCESS;
}

/* The root directory is inode 2 (inode 1 is reserved for bad blocks in ext2); its . and .. both point to itself. */
[[nodiscard]] static int create_root(EXT2_FILESYSTEM *fs) {
    BYTE block[MAX_BLOCK_SIZE] = {0};
    INODE root;
    UINT32 dataBlock;

    if (alloc_block(fs, &dataBlock))
        return EXT2_ERROR;

    fill_dot_entries(block, NUM_OF_ROOT_INODE, NUM_OF_ROOT_INODE);
    if (block_write(fs->disk, dataBlock, block))
        return EXT2_ERROR;

    ZeroMemory(&root, sizeof(root));
    root.mode = USER_PERMISSION | FILE_TYPE_DIR;
    root.links_count = 2;
    root.blocks = 1;
    root.size = MAX_BLOCK_SIZE;
    root.block[0] = dataBlock;
    if (put_inode(fs, NUM_OF_ROOT_INODE, &root))
        return EXT2_ERROR;
    return adjust_counts(fs, 0, 0, 0, 1);
}

int ext2_format(DISK_OPERATIONS *disk) {
    EXT2_SUPER_BLOCK sb;
    BYTE gdt[BLOCK_NUM_GDT_PER_GROUP * MAX_BLOCK_SIZE];
    EXT2_FILESYSTEM fs;
    UINT32 group;

    fill_super_block(&sb);
    fill_descriptor_table(gdt);
    for (group = 0; group < NUMBER_OF_GROUPS; group++)
        if (init_group(disk, group, &sb, gdt))
            return EXT2_ERROR;

    ZeroMemory(&fs, sizeof(fs));
    fs.disk = disk;
    if (block_read(disk, SUPER_BLOCK_BASE, &fs.sb))
        return EXT2_ERROR;
    return create_root(&fs);
}

int read_superblock(EXT2_FILESYSTEM *fs, EXT2_NODE *root) {
    BYTE block[MAX_BLOCK_SIZE];

    if (fs == nullptr || fs->disk == nullptr)
        return EXT2_ERROR;
    if (block_read(fs->disk, SUPER_BLOCK_BASE, block))
        return EXT2_ERROR;
    if (((EXT2_SUPER_BLOCK *)block)->magic_signature != EXT2_MAGIC)
        return EXT2_ERROR;
    memcpy(&fs->sb, block, sizeof(EXT2_SUPER_BLOCK));

    ZeroMemory(root, sizeof(EXT2_NODE));
    root->fs = fs;
    root->entry.inode = NUM_OF_ROOT_INODE;
    root->entry.name[0] = '/';
    root->entry.name_len = 1;
    return EXT2_SUCCESS;
}
