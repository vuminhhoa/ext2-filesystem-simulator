#include "disk/disksim.h"
#include "shell/ext2_shell.h"
#include "shell/shell.h"

#define COND_MOUNT 0x01u
#define COND_UMOUNT 0x02u

#define MAX_ARGS 100
#define MAX_PATH_DEPTH 256

typedef struct {
    const char *name;
    int (*handler)(int, char **);
    unsigned conditions;
} COMMAND;

static int shell_cmd_cd(int argc, char *argv[]);
static int shell_cmd_mount(int argc, char *argv[]);
static int shell_cmd_umount(int argc, char *argv[]);
static int shell_cmd_touch(int argc, char *argv[]);
static int shell_cmd_fill(int argc, char *argv[]);
static int shell_cmd_ls(int argc, char *argv[]);
static int shell_cmd_format(int argc, char *argv[]);
static int shell_cmd_mkdir(int argc, char *argv[]);
static int shell_cmd_rm(int argc, char *argv[]);
static int shell_cmd_rmdir(int argc, char *argv[]);
static int shell_cmd_exit(int argc, char *argv[]);
static int shell_cmd_cat(int argc, char *argv[]);

static const COMMAND g_commands[] = {
    {"cd", shell_cmd_cd, COND_MOUNT},
    {"mount", shell_cmd_mount, COND_UMOUNT},
    {"umount", shell_cmd_umount, COND_MOUNT},
    {"touch", shell_cmd_touch, COND_MOUNT},
    {"fill", shell_cmd_fill, COND_MOUNT},
    {"ls", shell_cmd_ls, COND_MOUNT},
    {"format", shell_cmd_format, COND_UMOUNT},
    {"mkdir", shell_cmd_mkdir, COND_MOUNT},
    {"rm", shell_cmd_rm, COND_MOUNT},
    {"rmdir", shell_cmd_rmdir, COND_MOUNT},
    {"exit", shell_cmd_exit, 0},
    {"cat", shell_cmd_cat, COND_MOUNT},
};

#define COMMAND_COUNT ((int)(sizeof(g_commands) / sizeof(g_commands[0])))

static SHELL_FILESYSTEM g_fs;
static SHELL_FS_OPERATIONS g_fsOprs;
static SHELL_ENTRY g_rootDir;
static SHELL_ENTRY g_currentDir;
static DISK_OPERATIONS g_disk;

static SHELL_ENTRY path[MAX_PATH_DEPTH];
static int pathTop = 0;

static int g_isMounted;
static int g_running = 1;

/******************************************************************************/
/*                            SHELL_COMMAND_LIST                              */
/******************************************************************************/
static int shell_cmd_cd(int argc, char *argv[]) {
    SHELL_ENTRY newEntry;

    path[0] = g_rootDir;

    if (argc > 2) {
        printf("usage : %s [directory]\n", argv[0]);
        return 0;
    }

    if (argc == 1)
        pathTop = 0;
    else if (strcmp(argv[1], ".") == 0)
        return 0;
    else if (strcmp(argv[1], "..") == 0) {
        if (pathTop > 0)
            pathTop--;
    } else {
        if (g_fsOprs.lookup(&g_disk, &g_fsOprs, &g_currentDir, &newEntry, argv[1])) {
            printf("directory not found\n");
            return -1;
        }
        if (!newEntry.isDirectory) {
            printf("%s is not a directory\n", argv[1]);
            return -1;
        }
        if (pathTop + 1 >= MAX_PATH_DEPTH) {
            printf("path is too deep\n");
            return -1;
        }
        path[++pathTop] = newEntry;
    }

    g_currentDir = path[pathTop];
    return 0;
}

static int shell_cmd_exit(int argc, char *argv[]) {
    (void)argc, (void)argv;
    g_running = 0;
    return 0;
}

static int shell_cmd_mount(int argc, char *argv[]) {
    (void)argc, (void)argv;

    if (g_fs.mount(&g_disk, &g_fsOprs, &g_rootDir) < 0) {
        printf("%s file system mounting has been failed\n", g_fs.name);
        return -1;
    }
    g_currentDir = g_rootDir;
    pathTop = 0;
    g_isMounted = 1;
    printf("%s file system has been mounted successfully\n", g_fs.name);
    return 0;
}

static int shell_cmd_umount(int argc, char *argv[]) {
    (void)argc, (void)argv;

    g_fs.umount(&g_disk, &g_fsOprs);
    g_isMounted = 0;
    return 0;
}

static int shell_cmd_touch(int argc, char *argv[]) {
    SHELL_ENTRY entry;
    int i, result = 0;

    if (argc < 2) {
        printf("usage : touch [files...]\n");
        return -2;
    }
    for (i = 1; i < argc; i++) {
        if (g_fsOprs.fileOprs->create(&g_disk, &g_fsOprs, &g_currentDir, argv[i], &entry)) {
            printf("create failed: %s\n", argv[i]);
            result = -1;
        }
    }
    return result;
}

/* Creates a file of `size` bytes filled with a repeating sentence. */
static int shell_cmd_fill(int argc, char *argv[]) {
    static const char pattern[] = "Can you see? ";
    SHELL_ENTRY entry;
    char *buffer;
    long size, i, written;

    if (argc != 3) {
        printf("usage : fill [file] [size]\n");
        return 0;
    }

    size = strtol(argv[2], nullptr, 10);
    if (size < 0) {
        printf("invalid size\n");
        return -1;
    }
    if (g_fsOprs.fileOprs->create(&g_disk, &g_fsOprs, &g_currentDir, argv[1], &entry)) {
        printf("create failed\n");
        return -1;
    }

    buffer = malloc((size_t)size + 1);
    if (buffer == nullptr) {
        printf("out of memory\n");
        return -1;
    }
    for (i = 0; i < size; i++)
        buffer[i] = pattern[i % (long)(sizeof(pattern) - 1)];

    written = g_fsOprs.fileOprs->write(&g_disk, &g_fsOprs, &g_currentDir, &entry, 0, (unsigned long)size, buffer);
    free(buffer);

    if (written != size) {
        printf("write failed (files hold at most %d bytes)\n", EXT2_D_BLOCKS * MAX_BLOCK_SIZE);
        return -1;
    }
    return 0;
}

static int shell_cmd_rm(int argc, char *argv[]) {
    int i;

    if (argc < 2) {
        printf("usage : rm [files...]\n");
        return 0;
    }
    for (i = 1; i < argc; i++)
        if (g_fsOprs.fileOprs->remove(&g_disk, &g_fsOprs, &g_currentDir, argv[i]))
            printf("cannot remove file\n");
    return 0;
}

static int shell_cmd_format(int argc, char *argv[]) {
    (void)argc, (void)argv;

    if (g_fs.format(&g_disk) < 0) {
        printf("%s formatting is failed\n", g_fs.name);
        return -1;
    }
    printf("disk has been formatted successfully\n");
    return 0;
}

static int shell_cmd_mkdir(int argc, char *argv[]) {
    SHELL_ENTRY entry;

    if (argc != 2) {
        printf("usage : %s [name]\n", argv[0]);
        return 0;
    }
    if (g_fsOprs.mkdir(&g_disk, &g_fsOprs, &g_currentDir, argv[1], &entry)) {
        printf("cannot create directory\n");
        return -1;
    }
    return 0;
}

static int shell_cmd_rmdir(int argc, char *argv[]) {
    if (argc != 2) {
        printf("usage : %s [name]\n", argv[0]);
        return 0;
    }
    if (g_fsOprs.rmdir(&g_disk, &g_fsOprs, &g_currentDir, argv[1])) {
        printf("cannot remove directory\n");
        return -1;
    }
    return 0;
}

static int shell_cmd_cat(int argc, char *argv[]) {
    SHELL_ENTRY entry;
    char buf[MAX_BLOCK_SIZE];
    unsigned long offset = 0;
    long result;

    if (argc != 2) {
        printf("usage : %s [file name]\n", argv[0]);
        return 0;
    }
    if (g_fsOprs.lookup(&g_disk, &g_fsOprs, &g_currentDir, &entry, argv[1])) {
        printf("%s lookup failed\n", argv[1]);
        return -1;
    }
    if (entry.isDirectory) {
        printf("%s is a directory\n", argv[1]);
        return -1;
    }

    while ((result = g_fsOprs.fileOprs->read(&g_disk, &g_fsOprs, &g_currentDir, &entry, offset, sizeof(buf), buf)) >
           0) {
        fwrite(buf, 1, (size_t)result, stdout);
        offset += (unsigned long)result;
    }
    printf("\n");
    return result < 0 ? -1 : 0;
}

static int shell_cmd_ls(int argc, char *argv[]) {
    SHELL_ENTRY_LIST list;
    SHELL_ENTRY_LIST_ITEM *current;

    if (argc > 1) {
        printf("usage : %s\n", argv[0]);
        return 0;
    }
    init_entry_list(&list);
    if (g_fsOprs.read_dir(&g_disk, &g_fsOprs, &g_currentDir, &list)) {
        printf("Failed to read_dir\n");
        release_entry_list(&list);
        return -1;
    }

    printf("[File names] [D] [File sizes]\n");
    for (current = list.first; current; current = current->next)
        printf("%-12s  %1d  %12u\n", (const char *)current->entry.name, current->entry.isDirectory,
               current->entry.size);
    printf("\n");

    release_entry_list(&list);
    return 0;
}

/******************************************************************************/
/*                            UTILITY FUNCTIONS                               */
/******************************************************************************/
static int check_conditions(unsigned conditions) {
    if ((conditions & COND_MOUNT) && !g_isMounted) {
        printf("file system is not mounted\n");
        return -1;
    }
    if ((conditions & COND_UMOUNT) && g_isMounted) {
        printf("file system is already mounted\n");
        return -1;
    }
    return 0;
}

static void unknown_command(void) {
    int i;

    printf(" * ");
    for (i = 0; i < COMMAND_COUNT; i++)
        printf(i < COMMAND_COUNT - 1 ? "%s, " : "%s", g_commands[i].name);
    printf("\n");
}

/* The C locale's white space, without going through <ctype.h>'s lookup table. */
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }

static int separate_string(char *buf, char *ptrs[], int max) {
    char prev = 0;
    int count = 0;

    while (*buf) {
        if (is_space(*buf))
            *buf = 0;
        else if (prev == 0) {
            if (count == max)
                break;
            ptrs[count++] = buf;
        }
        prev = *buf++;
    }
    return count;
}

static void do_shell(void) {
    char buf[1000];
    char *argv[MAX_ARGS] = {nullptr};
    int argc, i;

    printf("%s File system shell\n", g_fs.name);

    while (g_running) {
        printf("ext2 : [/%s]# ", (const char *)g_currentDir.name);
        fflush(stdout);
        if (fgets(buf, sizeof(buf), stdin) == nullptr) {
            printf("\n");
            break;
        }

        argc = separate_string(buf, argv, MAX_ARGS);
        if (argc == 0)
            continue;

        for (i = 0; i < COMMAND_COUNT; i++) {
            if (strcmp(g_commands[i].name, argv[0]) == 0) {
                if (check_conditions(g_commands[i].conditions) == 0)
                    g_commands[i].handler(argc, argv);
                break;
            }
        }
        if (i == COMMAND_COUNT)
            unknown_command();
    }
}

int main(void) {
    if (disksim_init(DISKSIM_NUMBER_OF_SECTORS, DISKSIM_SECTOR_SIZE, &g_disk) < 0) {
        printf("disk simulator initialization has been failed\n");
        return EXIT_FAILURE;
    }

    shell_register_filesystem(&g_fs);
    do_shell();

    if (g_isMounted)
        g_fs.umount(&g_disk, &g_fsOprs);
    disksim_uninit(&g_disk);
    return EXIT_SUCCESS;
}
