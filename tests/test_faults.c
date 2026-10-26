/*
 * Fault injection: the disk fails every sector read and write after a budget runs out. A scenario of
 * ordinary operations is replayed with the failure placed at each point in turn, and every call must
 * report the error instead of crashing or touching memory it should not (the sanitizers watch for that).
 */
#include "check.h"
#include "disk/disksim.h"
#include "ext2/ext2.h"

static long g_budget = -1; /* sector operations left before the disk fails; -1 never fails */
static long g_operations;

static int faulty_read(DISK_OPERATIONS *disk, SECTOR sector, void *data) {
    g_operations++;
    if (g_budget == 0)
        return -1;
    if (g_budget > 0)
        g_budget--;
    return disksim_read(disk, sector, data);
}

static int faulty_write(DISK_OPERATIONS *disk, SECTOR sector, const void *data) {
    g_operations++;
    if (g_budget == 0)
        return -1;
    if (g_budget > 0)
        g_budget--;
    return disksim_write(disk, sector, data);
}

/* Returns how many steps completed; a step that fails must fail cleanly. */
static int run_scenario(DISK_OPERATIONS *disk) {
    static char data[3000];
    char back[sizeof(data)];
    EXT2_FILESYSTEM fs = {.disk = disk};
    EXT2_NODE root, dir, file, found;
    int steps = 0;

    memset(data, 'x', sizeof(data));
    if (read_superblock(&fs, &root) != EXT2_SUCCESS)
        return steps;
    steps++;
    if (ext2_mkdir(&root, "docs", &dir) != EXT2_SUCCESS)
        return steps;
    steps++;
    if (ext2_create(&dir, "a.txt", &file) != EXT2_SUCCESS)
        return steps;
    steps++;
    if (ext2_write(&file, 0, sizeof(data), data) != (long)sizeof(data))
        return steps;
    steps++;
    if (ext2_read(&file, 0, sizeof(back), back) != (long)sizeof(back))
        return steps;
    CHECK(memcmp(back, data, sizeof(data)) == 0);
    steps++;
    if (ext2_lookup(&dir, "a.txt", &found) != EXT2_SUCCESS || ext2_remove(&dir, &found) != EXT2_SUCCESS)
        return steps;
    steps++;
    if (ext2_lookup(&root, "docs", &found) != EXT2_SUCCESS || ext2_rmdir(&root, &found) != EXT2_SUCCESS)
        return steps;
    return ++steps;
}

int main(void) {
    DISK_OPERATIONS disk;
    long total, budget;
    int completeSteps;

    if (disksim_init(DISKSIM_NUMBER_OF_SECTORS, DISKSIM_SECTOR_SIZE, &disk)) {
        fprintf(stderr, "cannot allocate the simulated disk\n");
        return EXIT_FAILURE;
    }
    disk.read_sector = faulty_read;
    disk.write_sector = faulty_write;

    /* A clean run, to learn how many sector operations the scenario takes. */
    CHECK(ext2_format(&disk) == EXT2_SUCCESS);
    g_operations = 0;
    completeSteps = run_scenario(&disk);
    total = g_operations;
    CHECK(completeSteps == 7);

    for (budget = 0; budget < total; budget++) {
        int steps;

        g_budget = -1;
        CHECK(ext2_format(&disk) == EXT2_SUCCESS);
        g_budget = budget;
        steps = run_scenario(&disk);
        CHECK(steps < completeSteps); /* the failure must surface as an error from some step */
    }

    /* Format itself must also fail cleanly at any point; checking a sample keeps the test fast. */
    for (budget = 0; budget < 70000; budget += 997) {
        g_budget = budget;
        (void)ext2_format(&disk);
    }

    g_budget = -1;
    disksim_uninit(&disk);
    printf("scenario takes %ld sector operations; each one failed in turn\n", total);
    return check_summary();
}
