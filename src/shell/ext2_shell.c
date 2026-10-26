#include "shell/ext2_shell.h"

#define FSOPRS_TO_EXT2FS(a) ((EXT2_FILESYSTEM *)(a)->pdata)

static_assert(sizeof(EXT2_NODE) <= sizeof(((SHELL_ENTRY *)0)->pdata), "an EXT2_NODE must fit in SHELL_ENTRY.pdata");

static int fs_mount(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, SHELL_ENTRY *root);
static void fs_umount(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs);
static int fs_format(DISK_OPERATIONS *disk);
static int fs_read_dir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent,
                       SHELL_ENTRY_LIST *list);
static int fs_mkdir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name,
                    SHELL_ENTRY *retEntry);
static int fs_rmdir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name);
static int fs_lookup(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                     const char *name);
static int fs_create(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name,
                     SHELL_ENTRY *retEntry);
static int fs_remove(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name);
static long fs_read(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                    unsigned long offset, unsigned long length, char *buffer);
static long fs_write(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                     unsigned long offset, unsigned long length, const char *buffer);

static SHELL_FILE_OPERATIONS g_file = {fs_create, fs_remove, fs_read, fs_write};

static SHELL_FS_OPERATIONS g_fsOprs = {fs_read_dir, nullptr, fs_mkdir, fs_rmdir, fs_lookup, &g_file, nullptr};

static SHELL_FILESYSTEM g_ext2 = {"EXT2", fs_mount, fs_umount, fs_format};

/******************************************************************************/
/*                          entry conversion                                  */
/******************************************************************************/
static void shell_entry_to_ext2_entry(const SHELL_ENTRY *shell_entry, EXT2_NODE *ext2_entry) {
    memcpy(ext2_entry, shell_entry->pdata, sizeof(EXT2_NODE));
}

/* Copies up to `length` characters of an 8.3 name part, stopping at the space padding. */
static char *copy_name_part(char *dest, const char *src, int length) {
    while (length-- > 0 && *src && *src != SPACE_CHAR)
        *dest++ = *src++;
    return dest;
}

static void ext2_entry_to_shell_entry(const EXT2_NODE *ext2_entry, SHELL_ENTRY *shell_entry) {
    UINT32 inodeNumber = ext2_entry->entry.inode;
    INODE inode;
    char *name;

    memset(shell_entry, 0, sizeof(SHELL_ENTRY));
    if (get_inode(ext2_entry->fs, inodeNumber, &inode))
        memset(&inode, 0, sizeof(inode));

    if (inodeNumber != NUM_OF_ROOT_INODE) {
        name = copy_name_part((char *)shell_entry->name, ext2_entry->entry.name, 8);
        if (ext2_entry->entry.name[8] != SPACE_CHAR) {
            *name++ = '.';
            copy_name_part(name, &ext2_entry->entry.name[8], 3);
        }
    }
    shell_entry->isDirectory = inodeNumber == NUM_OF_ROOT_INODE || (inode.mode & FILE_TYPE_MASK) == FILE_TYPE_DIR;
    shell_entry->size = inode.size;
    memcpy(shell_entry->pdata, ext2_entry, sizeof(EXT2_NODE));
}

static int adder(void *list, EXT2_NODE *entry) {
    SHELL_ENTRY newEntry;

    ext2_entry_to_shell_entry(entry, &newEntry);
    return add_entry_list((SHELL_ENTRY_LIST *)list, &newEntry);
}

/******************************************************************************/
/*                              FS_COMMAND                                    */
/******************************************************************************/
static int fs_mount(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, SHELL_ENTRY *root) {
    EXT2_FILESYSTEM *fs;
    EXT2_NODE ext2_entry;

    *fsOprs = g_fsOprs;
    fsOprs->pdata = calloc(1, sizeof(EXT2_FILESYSTEM));
    if (fsOprs->pdata == nullptr)
        return EXT2_ERROR;
    fs = FSOPRS_TO_EXT2FS(fsOprs);
    fs->disk = disk;

    if (read_superblock(fs, &ext2_entry)) {
        free(fsOprs->pdata);
        fsOprs->pdata = nullptr;
        return EXT2_ERROR;
    }

    printf("number of groups         : %u\n", (unsigned)NUMBER_OF_GROUPS);
    printf("blocks per group         : %u\n", fs->sb.block_per_group);
    printf("bytes per block          : %u\n", (unsigned)MAX_BLOCK_SIZE);
    printf("free block count         : %u\n", fs->sb.free_block_count);
    printf("free inode count         : %u\n", fs->sb.free_inode_count);
    printf("first non reserved inode : %u\n", fs->sb.first_non_reserved_inode);
    printf("inode structure size     : %u\n", fs->sb.inode_structure_size);
    printf("first data block number  : %u\n\n", fs->sb.first_data_block_each_group);

    ext2_entry_to_shell_entry(&ext2_entry, root);
    return EXT2_SUCCESS;
}

static void fs_umount(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs) {
    (void)disk;
    free(fsOprs->pdata);
    fsOprs->pdata = nullptr;
}

static int fs_format(DISK_OPERATIONS *disk) {
    printf("formatting as a EXT2\n");
    if (ext2_format(disk))
        return EXT2_ERROR;

    printf("max inode count                : %u\n", (unsigned)NUMBER_OF_INODES);
    printf("total block count              : %u\n", (unsigned)NUMBER_OF_BLOCK);
    printf("byte size of inode structure   : %u\n", (unsigned)INODE_SIZE);
    printf("block byte size                : %u\n", (unsigned)MAX_BLOCK_SIZE);
    printf("total sectors count            : %u\n", (unsigned)NUMBER_OF_SECTORS);
    printf("sector byte size               : %u\n\n", (unsigned)MAX_SECTOR_SIZE);
    return EXT2_SUCCESS;
}

static long fs_read(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                    unsigned long offset, unsigned long length, char *buffer) {
    EXT2_NODE file;

    (void)disk, (void)fsOprs, (void)parent;
    shell_entry_to_ext2_entry(entry, &file);
    return ext2_read(&file, offset, length, buffer);
}

static long fs_write(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                     unsigned long offset, unsigned long length, const char *buffer) {
    EXT2_NODE file;

    (void)disk, (void)fsOprs, (void)parent;
    shell_entry_to_ext2_entry(entry, &file);
    return ext2_write(&file, offset, length, buffer);
}

static int fs_create(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name,
                     SHELL_ENTRY *retEntry) {
    EXT2_NODE ext2Parent, ext2Entry;
    int result;

    (void)disk, (void)fsOprs;
    shell_entry_to_ext2_entry(parent, &ext2Parent);
    result = ext2_create(&ext2Parent, name, &ext2Entry);
    if (result == EXT2_SUCCESS)
        ext2_entry_to_shell_entry(&ext2Entry, retEntry);
    return result;
}

static int fs_mkdir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name,
                    SHELL_ENTRY *retEntry) {
    EXT2_NODE ext2Parent, ext2Entry;
    int result;

    (void)disk, (void)fsOprs;
    shell_entry_to_ext2_entry(parent, &ext2Parent);
    result = ext2_mkdir(&ext2Parent, name, &ext2Entry);
    if (result == EXT2_SUCCESS)
        ext2_entry_to_shell_entry(&ext2Entry, retEntry);
    return result;
}

static int fs_lookup(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, SHELL_ENTRY *entry,
                     const char *name) {
    EXT2_NODE ext2Parent, ext2Entry;
    int result;

    (void)disk, (void)fsOprs;
    shell_entry_to_ext2_entry(parent, &ext2Parent);
    result = ext2_lookup(&ext2Parent, name, &ext2Entry);
    if (result == EXT2_SUCCESS)
        ext2_entry_to_shell_entry(&ext2Entry, entry);
    return result;
}

static int fs_read_dir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent,
                       SHELL_ENTRY_LIST *list) {
    EXT2_NODE dir;

    (void)disk, (void)fsOprs;
    if (list->count)
        release_entry_list(list);
    shell_entry_to_ext2_entry(parent, &dir);
    return ext2_read_dir(&dir, adder, list);
}

static int fs_rmdir(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name) {
    EXT2_NODE ext2Parent, dir;

    (void)disk, (void)fsOprs;
    shell_entry_to_ext2_entry(parent, &ext2Parent);
    if (ext2_lookup(&ext2Parent, name, &dir))
        return EXT2_ERROR;
    return ext2_rmdir(&ext2Parent, &dir);
}

static int fs_remove(DISK_OPERATIONS *disk, SHELL_FS_OPERATIONS *fsOprs, const SHELL_ENTRY *parent, const char *name) {
    EXT2_NODE ext2Parent, file;

    (void)disk, (void)fsOprs;
    shell_entry_to_ext2_entry(parent, &ext2Parent);
    if (ext2_lookup(&ext2Parent, name, &file))
        return EXT2_ERROR;
    return ext2_remove(&ext2Parent, &file);
}

void shell_register_filesystem(SHELL_FILESYSTEM *fs) { *fs = g_ext2; }
