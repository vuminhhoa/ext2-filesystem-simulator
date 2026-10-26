/*
 * Tests for the ext2 simulator. Each test formats a fresh disk and ends with check_consistency(), which
 * recounts the bitmaps against the superblock and group descriptors (see fsck.c).
 */
#include "disk/disksim.h"
#include "ext2/ext2.h"
#include "check.h"
#include "fsck.h"

typedef struct {
    DISK_OPERATIONS disk;
    EXT2_FILESYSTEM fs;
    EXT2_NODE root;
} FIXTURE;

static void setup(FIXTURE *f) {
    memset(f, 0, sizeof(*f));
    if (disksim_init(DISKSIM_NUMBER_OF_SECTORS, DISKSIM_SECTOR_SIZE, &f->disk)) {
        fprintf(stderr, "cannot allocate the simulated disk\n");
        exit(EXIT_FAILURE);
    }
    CHECK(ext2_format(&f->disk) == EXT2_SUCCESS);
    f->fs.disk = &f->disk;
    CHECK(read_superblock(&f->fs, &f->root) == EXT2_SUCCESS);
}

static void teardown(FIXTURE *f) { disksim_uninit(&f->disk); }

static bool check_consistency(FIXTURE *f) { return fsck_counts_match(&f->disk, true); }

static UINT32 free_blocks(FIXTURE *f) { return fsck_free_blocks(&f->disk); }

static int count_adder(void *list, EXT2_NODE *entry) {
    (void)entry;
    (*(int *)list)++;
    return 0;
}

/******************************************************************************/

static void test_format_layout(void) {
    FIXTURE f;
    INODE root;
    UINT32 i;

    setup(&f);
    CHECK(f.fs.sb.magic_signature == EXT2_MAGIC);
    CHECK(NUMBER_OF_GROUPS == 32);
    CHECK(DATA_BLOCK_BASE == 517);
    CHECK(get_inode(&f.fs, NUM_OF_ROOT_INODE, &root) == EXT2_SUCCESS);
    CHECK((root.mode & FILE_TYPE_MASK) == FILE_TYPE_DIR);
    CHECK(root.blocks == 1);
    CHECK(root.block[0] == DATA_BLOCK_BASE);
    /* The root inode used to be built on the stack without being zeroed. */
    for (i = 1; i < EXT2_N_BLOCKS; i++)
        CHECK(root.block[i] == 0);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_names_are_8_3(void) {
    char out[MAX_ENTRY_NAME_LENGTH];

    CHECK(format_name("notes.txt", out) == EXT2_SUCCESS);
    CHECK(memcmp(out, "NOTES   TXT", MAX_ENTRY_NAME_LENGTH) == 0);
    CHECK(format_name("README", out) == EXT2_SUCCESS);
    CHECK(memcmp(out, "README     ", MAX_ENTRY_NAME_LENGTH) == 0);
    CHECK(format_name("..", out) == EXT2_SUCCESS);
    CHECK(format_name("ninechars.txt", out) == EXT2_ERROR);
    CHECK(format_name("a.text", out) == EXT2_ERROR);
    CHECK(format_name("a.b.c", out) == EXT2_ERROR);
    CHECK(format_name("bad-name", out) == EXT2_ERROR);
    CHECK(format_name("", out) == EXT2_ERROR);
    CHECK(format_name(".txt", out) == EXT2_ERROR);
}

static void test_create_lookup_and_duplicates(void) {
    FIXTURE f;
    EXT2_NODE a, found;

    setup(&f);
    CHECK(ext2_create(&f.root, "a.txt", &a) == EXT2_SUCCESS);
    CHECK(ext2_lookup(&f.root, "A.TXT", &found) == EXT2_SUCCESS);
    CHECK(found.entry.inode == a.entry.inode);
    CHECK(ext2_create(&f.root, "a.txt", &found) == EXT2_ERROR);
    CHECK(ext2_mkdir(&f.root, "a.txt", &found) == EXT2_ERROR);
    CHECK(ext2_lookup(&f.root, "missing", &found) == EXT2_ERROR);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_write_then_read(void) {
    FIXTURE f;
    EXT2_NODE file;
    char data[3000], back[4096];
    long i;

    setup(&f);
    for (i = 0; i < (long)sizeof(data); i++)
        data[i] = (char)('a' + i % 26);
    CHECK(ext2_create(&f.root, "big.txt", &file) == EXT2_SUCCESS);
    CHECK(ext2_write(&file, 0, sizeof(data), data) == (long)sizeof(data));

    /* ext2_read used to be handed the file system instead of the disk and crashed. */
    CHECK(ext2_read(&file, 0, sizeof(back), back) == (long)sizeof(data));
    CHECK(memcmp(back, data, sizeof(data)) == 0);
    CHECK(ext2_read(&file, 1000, 100, back) == 100);
    CHECK(memcmp(back, data + 1000, 100) == 0);
    CHECK(ext2_read(&file, sizeof(data), 10, back) == 0);

    CHECK(ext2_write(&file, 1020, 8, "OVERLAP!") == 8);
    CHECK(ext2_read(&file, 1018, 12, back) == 12);
    CHECK(memcmp(back + 2, "OVERLAP!", 8) == 0);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_files_are_limited_to_direct_blocks(void) {
    FIXTURE f;
    EXT2_NODE file;
    static char data[EXT2_D_BLOCKS * MAX_BLOCK_SIZE + 1];
    UINT32 before;

    setup(&f);
    CHECK(ext2_create(&f.root, "max.bin", &file) == EXT2_SUCCESS);
    before = free_blocks(&f);
    CHECK(ext2_write(&file, 0, sizeof(data), data) == EXT2_ERROR);
    CHECK(free_blocks(&f) == before);
    CHECK(ext2_write(&file, 0, sizeof(data) - 1, data) == (long)sizeof(data) - 1);
    CHECK(free_blocks(&f) == before - EXT2_D_BLOCKS);
    CHECK(check_consistency(&f));
    teardown(&f);
}

/* rm used to compute the bitmap's address with an unparenthesised macro and flip a bit inside a data block. */
static void test_remove_frees_and_keeps_other_files(void) {
    FIXTURE f;
    EXT2_NODE a, b, c, found;
    char data[2048], back[2048];
    UINT32 before;

    setup(&f);
    memset(data, 'B', sizeof(data));
    before = free_blocks(&f);
    CHECK(ext2_create(&f.root, "a.txt", &a) == EXT2_SUCCESS);
    CHECK(ext2_write(&a, 0, 10, "aaaaaaaaaa") == 10);
    CHECK(ext2_create(&f.root, "b.txt", &b) == EXT2_SUCCESS);
    CHECK(ext2_write(&b, 0, sizeof(data), data) == (long)sizeof(data));

    CHECK(ext2_lookup(&f.root, "a.txt", &found) == EXT2_SUCCESS);
    CHECK(ext2_remove(&f.root, &found) == EXT2_SUCCESS);
    CHECK(ext2_lookup(&f.root, "a.txt", &found) == EXT2_ERROR);
    CHECK(free_blocks(&f) == before - 2);
    CHECK(check_consistency(&f));

    CHECK(ext2_read(&b, 0, sizeof(back), back) == (long)sizeof(back));
    CHECK(memcmp(back, data, sizeof(data)) == 0);

    /* The freed inode and block are reused. */
    CHECK(ext2_create(&f.root, "c.txt", &c) == EXT2_SUCCESS);
    CHECK(c.entry.inode == a.entry.inode);
    CHECK(check_consistency(&f));
    teardown(&f);
}

/* Allocation used to scan only the first 2048 bits of group 0, so the disk was "full" at about 1.5 MiB. */
static void test_allocation_spans_groups(void) {
    FIXTURE f;
    EXT2_NODE dir, file;
    INODE inode;
    static char data[EXT2_D_BLOCKS * MAX_BLOCK_SIZE];
    char name[16];
    int d, i, files = 0;

    setup(&f);
    /* 12 directories x 300 files x 12 KiB is about 42 MiB: past group 0 and into several others. */
    for (d = 0; d < 12; d++) {
        snprintf(name, sizeof(name), "d%d", d);
        CHECK(ext2_mkdir(&f.root, name, &dir) == EXT2_SUCCESS);
        for (i = 0; i < 300; i++) {
            snprintf(name, sizeof(name), "f%d", i);
            if (ext2_create(&dir, name, &file) != EXT2_SUCCESS || ext2_write(&file, 0, sizeof(data), data) < 0)
                break;
            files++;
        }
    }
    CHECK(files == 12 * 300);
    /* 43,200 data blocks: the last file lands in group 5. */
    CHECK(get_inode(&f.fs, file.entry.inode, &inode) == EXT2_SUCCESS);
    CHECK(GET_DATA_GROUP(inode.block[0]) == 5);
    CHECK(check_consistency(&f));
    teardown(&f);
}

/* A full directory block used to get a new block that was never linked to the directory's inode. */
static void test_directory_grows_past_one_block(void) {
    FIXTURE f;
    EXT2_NODE file;
    INODE root;
    char name[16];
    int i, listed = 0, found = 0;

    setup(&f);
    for (i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "f%d", i);
        CHECK(ext2_create(&f.root, name, &file) == EXT2_SUCCESS);
    }
    CHECK(get_inode(&f.fs, NUM_OF_ROOT_INODE, &root) == EXT2_SUCCESS);
    CHECK(root.blocks == 4);
    CHECK(ext2_read_dir(&f.root, count_adder, &listed) == EXT2_SUCCESS);
    CHECK(listed == 100);
    for (i = 0; i < 100; i++) {
        snprintf(name, sizeof(name), "f%d", i);
        found += ext2_lookup(&f.root, name, &file) == EXT2_SUCCESS;
    }
    CHECK(found == 100);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_directory_limit_is_reported(void) {
    FIXTURE f;
    EXT2_NODE file;
    char name[16];
    int i, created = 0;

    setup(&f);
    for (i = 0; i < 400; i++) {
        snprintf(name, sizeof(name), "f%d", i);
        if (ext2_create(&f.root, name, &file) != EXT2_SUCCESS)
            break;
        created++;
    }
    /* 12 blocks x 32 slots, minus . and .. */
    CHECK(created == EXT2_D_BLOCKS * (int)ENTRY_PER_BLOCK - 2);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_mkdir_and_rmdir(void) {
    FIXTURE f;
    EXT2_NODE dir, sub, file, found;
    UINT32 before;
    int listed = 0;

    setup(&f);
    before = free_blocks(&f);
    CHECK(ext2_mkdir(&f.root, "docs", &dir) == EXT2_SUCCESS);
    CHECK(ext2_lookup(&dir, ".", &found) == EXT2_SUCCESS);
    CHECK(found.entry.inode == dir.entry.inode);
    CHECK(ext2_lookup(&dir, "..", &found) == EXT2_SUCCESS);
    CHECK(found.entry.inode == NUM_OF_ROOT_INODE);
    CHECK(ext2_read_dir(&dir, count_adder, &listed) == EXT2_SUCCESS);
    CHECK(listed == 0);

    CHECK(ext2_mkdir(&dir, "sub", &sub) == EXT2_SUCCESS);
    CHECK(ext2_create(&sub, "x.txt", &file) == EXT2_SUCCESS);
    CHECK(ext2_rmdir(&dir, &sub) == EXT2_ERROR);  /* not empty */
    CHECK(ext2_remove(&dir, &sub) == EXT2_ERROR); /* rm refuses directories */
    CHECK(ext2_lookup(&sub, "x.txt", &found) == EXT2_SUCCESS);
    CHECK(ext2_remove(&sub, &found) == EXT2_SUCCESS);
    CHECK(ext2_rmdir(&dir, &sub) == EXT2_SUCCESS);

    CHECK(ext2_lookup(&dir, ".", &found) == EXT2_SUCCESS);
    CHECK(ext2_rmdir(&dir, &found) == EXT2_ERROR); /* never remove . */
    CHECK(ext2_rmdir(&f.root, &dir) == EXT2_SUCCESS);
    CHECK(free_blocks(&f) == before);
    CHECK(check_consistency(&f));
    teardown(&f);
}

static void test_remount_after_reformat(void) {
    FIXTURE f;
    EXT2_NODE file, found;

    setup(&f);
    CHECK(ext2_create(&f.root, "old.txt", &file) == EXT2_SUCCESS);
    CHECK(ext2_format(&f.disk) == EXT2_SUCCESS);
    CHECK(read_superblock(&f.fs, &f.root) == EXT2_SUCCESS);
    CHECK(ext2_lookup(&f.root, "old.txt", &found) == EXT2_ERROR);
    CHECK(check_consistency(&f));
    teardown(&f);
}

int main(void) {
    test_format_layout();
    test_names_are_8_3();
    test_create_lookup_and_duplicates();
    test_write_then_read();
    test_files_are_limited_to_direct_blocks();
    test_remove_frees_and_keeps_other_files();
    test_allocation_spans_groups();
    test_directory_grows_past_one_block();
    test_directory_limit_is_reported();
    test_mkdir_and_rmdir();
    test_remount_after_reformat();

    return check_summary();
}
