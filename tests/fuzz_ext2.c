/*
 * Fuzzer: the input is read as a sequence of operations (mkdir, rmdir, create, write, read, rm) on a few
 * fixed names. Each result is compared with a small in-memory model of what the file system should hold,
 * and after every operation the bitmaps must still agree with the free counts.
 *
 * Built with -fsanitize=fuzzer it is a libFuzzer target. Without it, a small driver runs inputs given as
 * files, or random ones: fuzz_ext2 [--iterations N] [input files...]
 */
#include "disk/disksim.h"
#include "ext2/ext2.h"
#include "fsck.h"

#define DIRS 4
#define PARENTS (DIRS + 1) /* the root, then D0..D3 */
#define FILES 8
#define MAX_FILE_SIZE (EXT2_D_BLOCKS * MAX_BLOCK_SIZE)
#define MAX_OPS 256

#define FUZZ_ASSERT(cond, ...)                                                                                         \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            fprintf(stderr, "%s:%d: %s: ", __FILE__, __LINE__, #cond);                                                 \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fprintf(stderr, "\n");                                                                                     \
            abort();                                                                                                   \
        }                                                                                                              \
    } while (0)

typedef struct {
    bool exists;
    UINT32 size;
    BYTE data[MAX_FILE_SIZE];
} MODEL_FILE;

typedef struct {
    bool dirExists[DIRS];
    MODEL_FILE files[PARENTS][FILES];
} MODEL;

typedef struct {
    const BYTE *data;
    size_t size;
} INPUT;

static DISK_OPERATIONS g_disk;
static EXT2_FILESYSTEM g_fs;
static MODEL g_model;
static char g_buffer[MAX_FILE_SIZE];

static BYTE next_byte(INPUT *in) {
    BYTE value = 0;

    if (in->size > 0) {
        value = *in->data++;
        in->size--;
    }
    return value;
}

static UINT32 next_u16(INPUT *in) {
    UINT32 high = next_byte(in);
    return (high << 8u) | next_byte(in);
}

static void dir_name(char *out, size_t size, UINT32 d) { snprintf(out, size, "d%u", d); }

static void file_name(char *out, size_t size, UINT32 f) { snprintf(out, size, "f%u.dat", f); }

/* Finds the node for parent `p`; fails if that directory does not exist. */
static bool parent_node(UINT32 p, EXT2_NODE *root, EXT2_NODE *node) {
    char name[16];

    if (p == 0) {
        *node = *root;
        return true;
    }
    dir_name(name, sizeof(name), p - 1);
    return ext2_lookup(root, name, node) == EXT2_SUCCESS;
}

static bool dir_is_empty(UINT32 d) {
    UINT32 f;

    for (f = 0; f < FILES; f++)
        if (g_model.files[d + 1][f].exists)
            return false;
    return true;
}

static void verify_file(EXT2_NODE *parent, UINT32 p, UINT32 f) {
    MODEL_FILE *model = &g_model.files[p][f];
    EXT2_NODE node;
    char name[16];
    long n;

    file_name(name, sizeof(name), f);
    FUZZ_ASSERT((ext2_lookup(parent, name, &node) == EXT2_SUCCESS) == model->exists, "lookup %u/%s", p, name);
    if (!model->exists)
        return;
    n = ext2_read(&node, 0, sizeof(g_buffer), g_buffer);
    FUZZ_ASSERT(n == (long)model->size, "read %u/%s returned %ld, expected %u", p, name, n, model->size);
    FUZZ_ASSERT(memcmp(g_buffer, model->data, model->size) == 0, "contents of %u/%s differ", p, name);
}

static void run_operation(INPUT *in, EXT2_NODE *root) {
    BYTE op = next_byte(in) % 6;
    BYTE target = next_byte(in);
    UINT32 d = target % DIRS, p = target % PARENTS, f = ((UINT32)target >> 4u) % FILES;
    EXT2_NODE parent, node;
    char name[16];
    bool parentExists = p == 0 || g_model.dirExists[p - 1];
    MODEL_FILE *file = &g_model.files[p][f];
    int result;

    switch (op) {
    case 0: /* mkdir dN */
        dir_name(name, sizeof(name), d);
        result = ext2_mkdir(root, name, &node);
        FUZZ_ASSERT((result == EXT2_SUCCESS) == !g_model.dirExists[d], "mkdir %s", name);
        g_model.dirExists[d] = true;
        break;

    case 1: /* rmdir dN */
        dir_name(name, sizeof(name), d);
        result = ext2_lookup(root, name, &node) == EXT2_SUCCESS ? ext2_rmdir(root, &node) : EXT2_ERROR;
        FUZZ_ASSERT((result == EXT2_SUCCESS) == (g_model.dirExists[d] && dir_is_empty(d)), "rmdir %s", name);
        if (result == EXT2_SUCCESS)
            g_model.dirExists[d] = false;
        break;

    case 2: /* touch fN.dat */
        file_name(name, sizeof(name), f);
        result = parent_node(p, root, &parent) ? ext2_create(&parent, name, &node) : EXT2_ERROR;
        FUZZ_ASSERT((result == EXT2_SUCCESS) == (parentExists && !file->exists), "create %u/%s", p, name);
        if (result == EXT2_SUCCESS) {
            file->exists = true;
            file->size = 0;
        }
        break;

    case 3: { /* write */
        UINT32 offset = next_u16(in) % (MAX_FILE_SIZE + 1);
        UINT32 length = next_u16(in) % 2049;
        BYTE fill = next_byte(in);
        bool fits = offset + length <= MAX_FILE_SIZE;
        long written = EXT2_ERROR;

        file_name(name, sizeof(name), f);
        memset(g_buffer, fill, length);
        if (parent_node(p, root, &parent) && ext2_lookup(&parent, name, &node) == EXT2_SUCCESS)
            written = ext2_write(&node, offset, length, g_buffer);
        FUZZ_ASSERT((written == (long)length) == (file->exists && fits), "write %u/%s at %u, %u bytes: %ld", p, name,
                    offset, length, written);
        if (written == (long)length) {
            memset(file->data + offset, fill, length);
            if (offset + length > file->size)
                file->size = offset + length;
        }
        break;
    }

    case 4: /* read back */
        if (parent_node(p, root, &parent))
            verify_file(&parent, p, f);
        break;

    case 5: /* rm fN.dat */
        file_name(name, sizeof(name), f);
        result = parent_node(p, root, &parent) && ext2_lookup(&parent, name, &node) == EXT2_SUCCESS
                     ? ext2_remove(&parent, &node)
                     : EXT2_ERROR;
        FUZZ_ASSERT((result == EXT2_SUCCESS) == file->exists, "rm %u/%s", p, name);
        if (result == EXT2_SUCCESS)
            memset(file, 0, sizeof(*file));
        break;

    default: /* unreachable: op is taken modulo 6 */
        break;
    }

    FUZZ_ASSERT(fsck_counts_match(&g_disk, true), "free counts disagree with the bitmaps after op %u", op);
}

/* The entry point libFuzzer calls, so it must have external linkage. */
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size); // NOLINT(misc-use-internal-linkage)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    INPUT in = {data, size};
    EXT2_NODE root, parent;
    UINT32 p, f;
    int ops = 0;

    if (g_disk.pdata == nullptr && disksim_init(DISKSIM_NUMBER_OF_SECTORS, DISKSIM_SECTOR_SIZE, &g_disk))
        abort();
    memset(&g_model, 0, sizeof(g_model));
    memset(&g_fs, 0, sizeof(g_fs));
    g_fs.disk = &g_disk;
    FUZZ_ASSERT(ext2_format(&g_disk) == EXT2_SUCCESS, "format");
    FUZZ_ASSERT(read_superblock(&g_fs, &root) == EXT2_SUCCESS, "mount");

    while (in.size > 0 && ops++ < MAX_OPS)
        run_operation(&in, &root);

    for (p = 0; p < PARENTS; p++)
        if (parent_node(p, &root, &parent))
            for (f = 0; f < FILES; f++)
                verify_file(&parent, p, f);
    return 0;
}

#ifndef EXT2SIM_LIBFUZZER
static UINT64 g_seed = 0x9E3779B97F4A7C15u;

static UINT64 xorshift(void) {
    g_seed ^= g_seed << 13u;
    g_seed ^= g_seed >> 7u;
    g_seed ^= g_seed << 17u;
    return g_seed;
}

static int run_file(const char *path) {
    static BYTE data[1u << 16u];
    FILE *file = fopen(path, "rb");
    size_t size;

    if (file == nullptr) {
        perror(path);
        return 1;
    }
    size = fread(data, 1, sizeof(data), file);
    fclose(file);
    return LLVMFuzzerTestOneInput(data, size);
}

int main(int argc, char **argv) {
    static BYTE data[1024];
    long iterations = 200, i;
    size_t j;
    int first = 1;

    if (argc >= 3 && strcmp(argv[1], "--iterations") == 0) {
        iterations = strtol(argv[2], nullptr, 10);
        first = 3;
    }
    if (first < argc) {
        for (; first < argc; first++)
            if (run_file(argv[first]))
                return EXIT_FAILURE;
        return EXIT_SUCCESS;
    }

    for (i = 0; i < iterations; i++) {
        size_t size = (size_t)(xorshift() % sizeof(data));

        for (j = 0; j < size; j++)
            data[j] = (BYTE)xorshift();
        LLVMFuzzerTestOneInput(data, size);
    }
    printf("%ld random inputs, no failures\n", iterations);
    disksim_uninit(&g_disk);
    return EXIT_SUCCESS;
}
#endif
