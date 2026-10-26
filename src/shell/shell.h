#ifndef EXT2SIM_SHELL_H
#define EXT2SIM_SHELL_H

#include "disk/disk.h"

typedef struct SHELL_ENTRY {
    struct SHELL_ENTRY *parent;

    unsigned char name[256];
    unsigned char isDirectory;
    unsigned int size;

    /* Filesystem-specific node, opaque to the shell. */
    char pdata[1024];
} SHELL_ENTRY;

typedef struct SHELL_ENTRY_LIST_ITEM {
    struct SHELL_ENTRY entry;
    struct SHELL_ENTRY_LIST_ITEM *next;
} SHELL_ENTRY_LIST_ITEM;

typedef struct {
    unsigned int count;
    SHELL_ENTRY_LIST_ITEM *first;
    SHELL_ENTRY_LIST_ITEM *last;
} SHELL_ENTRY_LIST;

struct SHELL_FILE_OPERATIONS;

typedef struct SHELL_FS_OPERATIONS {
    int (*read_dir)(DISK_OPERATIONS *, struct SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, SHELL_ENTRY_LIST *);
    int (*stat)(DISK_OPERATIONS *, struct SHELL_FS_OPERATIONS *, unsigned int *, unsigned int *);
    int (*mkdir)(DISK_OPERATIONS *, struct SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, const char *, SHELL_ENTRY *);
    int (*rmdir)(DISK_OPERATIONS *, struct SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, const char *);
    int (*lookup)(DISK_OPERATIONS *, struct SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, SHELL_ENTRY *, const char *);

    struct SHELL_FILE_OPERATIONS *fileOprs;
    void *pdata;
} SHELL_FS_OPERATIONS;

typedef struct SHELL_FILE_OPERATIONS {
    int (*create)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, const char *, SHELL_ENTRY *);
    int (*remove)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, const char *);
    /* read and write return the number of bytes transferred, or a negative value on error. */
    long (*read)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, SHELL_ENTRY *, unsigned long,
                 unsigned long, char *);
    long (*write)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *, const SHELL_ENTRY *, SHELL_ENTRY *, unsigned long,
                  unsigned long, const char *);
} SHELL_FILE_OPERATIONS;

typedef struct {
    const char *name;
    int (*mount)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *, SHELL_ENTRY *);
    void (*umount)(DISK_OPERATIONS *, SHELL_FS_OPERATIONS *);
    int (*format)(DISK_OPERATIONS *);
} SHELL_FILESYSTEM;

int init_entry_list(SHELL_ENTRY_LIST *list);
[[nodiscard]] int add_entry_list(SHELL_ENTRY_LIST *list, const SHELL_ENTRY *entry);
void release_entry_list(SHELL_ENTRY_LIST *list);

#endif
