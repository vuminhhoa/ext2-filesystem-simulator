/* Directory entries: lookup, insertion and listing. */
#include "ext2/internal.h"

const char DOT_NAME[MAX_ENTRY_NAME_LENGTH] = {'.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
const char DOTDOT_NAME[MAX_ENTRY_NAME_LENGTH] = {'.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};

/* Writes the . and .. entries that start every directory's first block. */
void fill_dot_entries(BYTE block[MAX_BLOCK_SIZE], UINT32 self, UINT32 parent) {
    EXT2_DIR_ENTRY *entries = (EXT2_DIR_ENTRY *)block;

    memcpy(entries[0].name, DOT_NAME, MAX_ENTRY_NAME_LENGTH);
    entries[0].inode = self;
    entries[0].name_len = 1;
    memcpy(entries[1].name, DOTDOT_NAME, MAX_ENTRY_NAME_LENGTH);
    entries[1].inode = parent;
    entries[1].name_len = 2;
}

int set_entry(EXT2_FILESYSTEM *fs, const EXT2_DIR_ENTRY_LOCATION *location, const EXT2_DIR_ENTRY *value) {
    BYTE block[MAX_BLOCK_SIZE];

    if (block_read(fs->disk, location->block, block))
        return EXT2_ERROR;
    memcpy(block + location->offset * sizeof(EXT2_DIR_ENTRY), value, sizeof(EXT2_DIR_ENTRY));
    return block_write(fs->disk, location->block, block);
}

bool is_dot_entry(const EXT2_DIR_ENTRY *entry) {
    return memcmp(entry->name, DOT_NAME, MAX_ENTRY_NAME_LENGTH) == 0 ||
           memcmp(entry->name, DOTDOT_NAME, MAX_ENTRY_NAME_LENGTH) == 0;
}

/*
 * Every slot is used, free (DIR_ENTRY_FREE) or the end of the directory (DIR_ENTRY_NO_MORE, which is
 * also what a zeroed block holds). With `name` nullptr, finds the first entry other than . and ..
 */
int lookup_entry(EXT2_FILESYSTEM *fs, UINT32 inode, const char *name, EXT2_NODE *retEntry) {
    BYTE block[MAX_BLOCK_SIZE];
    EXT2_DIR_ENTRY *entries = (EXT2_DIR_ENTRY *)block;
    INODE dir;
    UINT32 i, slot;

    if (get_inode(fs, inode, &dir) || !is_directory(&dir))
        return EXT2_ERROR;

    for (i = 0; i < dir.blocks && i < EXT2_D_BLOCKS; i++) {
        if (block_read(fs->disk, dir.block[i], block))
            return EXT2_ERROR;
        for (slot = 0; slot < ENTRY_PER_BLOCK; slot++) {
            EXT2_DIR_ENTRY *entry = &entries[slot];

            if (entry->name[0] == DIR_ENTRY_NO_MORE)
                return EXT2_ERROR;
            if (entry->name[0] == DIR_ENTRY_FREE)
                continue;
            if (name ? memcmp(entry->name, name, MAX_ENTRY_NAME_LENGTH) != 0 : is_dot_entry(entry))
                continue;

            retEntry->fs = fs;
            retEntry->entry = *entry;
            retEntry->location.group = GET_INODE_GROUP(entry->inode);
            retEntry->location.block = dir.block[i];
            retEntry->location.offset = slot;
            return EXT2_SUCCESS;
        }
    }
    return EXT2_ERROR;
}

/* Puts `entry` in the first free slot of the directory, adding a block when every slot is taken. */
int insert_entry(EXT2_NODE *parent, const EXT2_DIR_ENTRY *entry, EXT2_DIR_ENTRY_LOCATION *location) {
    EXT2_FILESYSTEM *fs = parent->fs;
    BYTE block[MAX_BLOCK_SIZE];
    EXT2_DIR_ENTRY *entries = (EXT2_DIR_ENTRY *)block;
    INODE dir;
    UINT32 i, slot, newBlock;

    if (get_inode(fs, parent->entry.inode, &dir))
        return EXT2_ERROR;

    for (i = 0; i < dir.blocks && i < EXT2_D_BLOCKS; i++) {
        if (block_read(fs->disk, dir.block[i], block))
            return EXT2_ERROR;
        for (slot = 0; slot < ENTRY_PER_BLOCK; slot++) {
            if (entries[slot].name[0] != DIR_ENTRY_FREE && entries[slot].name[0] != DIR_ENTRY_NO_MORE)
                continue;
            location->group = GET_INODE_GROUP(entry->inode);
            location->block = dir.block[i];
            location->offset = slot;
            return set_entry(fs, location, entry);
        }
    }

    if (dir.blocks >= EXT2_D_BLOCKS || alloc_block(fs, &newBlock))
        return EXT2_ERROR;
    dir.block[dir.blocks++] = newBlock;
    dir.size = dir.blocks * MAX_BLOCK_SIZE;
    if (put_inode(fs, parent->entry.inode, &dir))
        return EXT2_ERROR;

    location->group = GET_INODE_GROUP(entry->inode);
    location->block = newBlock;
    location->offset = 0;
    return set_entry(fs, location, entry);
}

[[nodiscard]] static int read_dir_from_block(EXT2_FILESYSTEM *fs, UINT32 blockNumber, EXT2_NODE_ADD adder, void *list) {
    BYTE block[MAX_BLOCK_SIZE];
    EXT2_DIR_ENTRY *entries = (EXT2_DIR_ENTRY *)block;
    EXT2_NODE node;
    UINT32 slot;

    if (block_read(fs->disk, blockNumber, block))
        return EXT2_ERROR;
    for (slot = 0; slot < ENTRY_PER_BLOCK; slot++) {
        if (entries[slot].name[0] == DIR_ENTRY_NO_MORE)
            return 1;
        if (entries[slot].name[0] == DIR_ENTRY_FREE || is_dot_entry(&entries[slot]))
            continue;
        ZeroMemory(&node, sizeof(node));
        node.fs = fs;
        node.entry = entries[slot];
        node.location.group = GET_INODE_GROUP(entries[slot].inode);
        node.location.block = blockNumber;
        node.location.offset = slot;
        adder(list, &node);
    }
    return EXT2_SUCCESS;
}

int ext2_read_dir(EXT2_NODE *dir, EXT2_NODE_ADD adder, void *list) {
    INODE inode;
    UINT32 i;
    int result;

    if (get_inode(dir->fs, dir->entry.inode, &inode) || !is_directory(&inode))
        return EXT2_ERROR;

    for (i = 0; i < inode.blocks && i < EXT2_D_BLOCKS; i++) {
        result = read_dir_from_block(dir->fs, inode.block[i], adder, list);
        if (result == EXT2_ERROR)
            return EXT2_ERROR;
        if (result == 1)
            break;
    }
    return EXT2_SUCCESS;
}
