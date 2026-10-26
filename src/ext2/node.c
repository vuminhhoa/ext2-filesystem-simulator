/* Creating and removing files and directories. */
#include "ext2/internal.h"

int ext2_lookup(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry) {
    char formattedName[MAX_ENTRY_NAME_LENGTH];

    if (format_name(entryName, formattedName))
        return EXT2_ERROR;
    return lookup_entry(parent->fs, parent->entry.inode, formattedName, retEntry);
}

/* Shared by create and mkdir: a new inode plus its entry in `parent`, undone if either step fails. */
[[nodiscard]] static int create_node(EXT2_NODE *parent, const char *entryName, INODE *inode, EXT2_NODE *retEntry) {
    EXT2_FILESYSTEM *fs = parent->fs;
    EXT2_NODE existing;
    EXT2_DIR_ENTRY entry;
    UINT32 inodeNumber;

    ZeroMemory(&entry, sizeof(entry));
    if (format_name(entryName, entry.name) || is_dot_entry(&entry))
        return EXT2_ERROR;
    if (lookup_entry(fs, parent->entry.inode, entry.name, &existing) == EXT2_SUCCESS)
        return EXT2_ERROR;
    if (alloc_inode(fs, &inodeNumber))
        return EXT2_ERROR;

    entry.inode = inodeNumber;
    entry.name_len = name_length(entry.name);
    if (put_inode(fs, inodeNumber, inode) || insert_entry(parent, &entry, &retEntry->location)) {
        INODE empty;

        ZeroMemory(&empty, sizeof(empty));
        /* Best effort: the original failure is what gets reported. */
        (void)put_inode(fs, inodeNumber, &empty);
        (void)free_inode(fs, inodeNumber);
        return EXT2_ERROR;
    }

    retEntry->fs = fs;
    retEntry->entry = entry;
    return EXT2_SUCCESS;
}

int ext2_create(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry) {
    INODE inode;

    ZeroMemory(retEntry, sizeof(EXT2_NODE));
    ZeroMemory(&inode, sizeof(inode));
    inode.mode = USER_PERMISSION | FILE_TYPE_FILE;
    inode.links_count = 1;
    return create_node(parent, entryName, &inode, retEntry);
}

int ext2_mkdir(EXT2_NODE *parent, const char *entryName, EXT2_NODE *retEntry) {
    EXT2_FILESYSTEM *fs = parent->fs;
    BYTE block[MAX_BLOCK_SIZE] = {0};
    INODE inode, parentInode;
    UINT32 dataBlock;

    ZeroMemory(retEntry, sizeof(EXT2_NODE));
    if (alloc_block(fs, &dataBlock))
        return EXT2_ERROR;

    ZeroMemory(&inode, sizeof(inode));
    inode.mode = USER_PERMISSION | FILE_TYPE_DIR;
    inode.links_count = 2;
    inode.blocks = 1;
    inode.size = MAX_BLOCK_SIZE;
    inode.block[0] = dataBlock;
    if (create_node(parent, entryName, &inode, retEntry)) {
        (void)free_block(fs, dataBlock);
        return EXT2_ERROR;
    }

    fill_dot_entries(block, retEntry->entry.inode, parent->entry.inode);
    if (block_write(fs->disk, dataBlock, block))
        return EXT2_ERROR;

    if (get_inode(fs, parent->entry.inode, &parentInode))
        return EXT2_ERROR;
    parentInode.links_count++;
    if (put_inode(fs, parent->entry.inode, &parentInode))
        return EXT2_ERROR;
    return adjust_counts(fs, GET_INODE_GROUP(retEntry->entry.inode), 0, 0, 1);
}

/* Frees the inode's blocks, the inode and its directory entry. */
[[nodiscard]] static int remove_node(EXT2_NODE *parent, EXT2_NODE *node, INODE *inode) {
    EXT2_FILESYSTEM *fs = parent->fs;
    EXT2_DIR_ENTRY freed;
    INODE empty;

    if (release_blocks(fs, inode))
        return EXT2_ERROR;
    ZeroMemory(&empty, sizeof(empty));
    if (put_inode(fs, node->entry.inode, &empty) || free_inode(fs, node->entry.inode))
        return EXT2_ERROR;

    ZeroMemory(&freed, sizeof(freed));
    freed.name[0] = DIR_ENTRY_FREE;
    return set_entry(fs, &node->location, &freed);
}

int ext2_remove(EXT2_NODE *parent, EXT2_NODE *rmfile) {
    INODE inode;

    rmfile->fs = parent->fs;
    if (get_inode(parent->fs, rmfile->entry.inode, &inode) || is_directory(&inode))
        return EXT2_ERROR;
    return remove_node(parent, rmfile, &inode);
}

int ext2_rmdir(EXT2_NODE *parent, EXT2_NODE *rmdir) {
    EXT2_FILESYSTEM *fs = parent->fs;
    EXT2_NODE child;
    INODE inode, parentInode;

    rmdir->fs = fs;
    if (is_dot_entry(&rmdir->entry) || rmdir->entry.inode == NUM_OF_ROOT_INODE)
        return EXT2_ERROR;
    if (get_inode(fs, rmdir->entry.inode, &inode) || !is_directory(&inode))
        return EXT2_ERROR;
    if (lookup_entry(fs, rmdir->entry.inode, nullptr, &child) == EXT2_SUCCESS)
        return EXT2_ERROR; /* not empty */

    if (remove_node(parent, rmdir, &inode))
        return EXT2_ERROR;

    if (get_inode(fs, parent->entry.inode, &parentInode))
        return EXT2_ERROR;
    parentInode.links_count--;
    if (put_inode(fs, parent->entry.inode, &parentInode))
        return EXT2_ERROR;
    return adjust_counts(fs, GET_INODE_GROUP(rmdir->entry.inode), 0, 0, -1);
}
