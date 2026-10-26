#ifndef EXT2SIM_EXT2_LAYOUT_H
#define EXT2SIM_EXT2_LAYOUT_H

/* The on-disk format: geometry, group layout and the structures stored in blocks. */

#include "common/common.h"
#include "disk/disksim.h"

#define MAX_SECTOR_SIZE DISKSIM_SECTOR_SIZE
#define MAX_BLOCK_SIZE (MAX_SECTOR_SIZE * 2)
#define TOTAL_DISK_SIZE DISKSIM_DISK_SIZE

#define MAX_NAME_LENGTH 256
#define MAX_ENTRY_NAME_LENGTH 11

#define EXT2_D_BLOCKS 12
#define EXT2_N_BLOCKS 15

/* One block of bitmap tracks 8 * MAX_BLOCK_SIZE blocks, which sets the size of a group. */
#define BLOCKS_PER_GROUP (8 * MAX_BLOCK_SIZE)
#define GROUP_SIZE (BLOCKS_PER_GROUP * MAX_BLOCK_SIZE)

#define NUMBER_OF_SECTORS (TOTAL_DISK_SIZE / MAX_SECTOR_SIZE)
#define NUMBER_OF_BLOCK (TOTAL_DISK_SIZE / MAX_BLOCK_SIZE)
#define NUMBER_OF_GROUPS ((TOTAL_DISK_SIZE - MAX_BLOCK_SIZE) / GROUP_SIZE)
#define NUMBER_OF_INODES (NUMBER_OF_BLOCK / 2)
#define INODES_PER_GROUP (NUMBER_OF_INODES / NUMBER_OF_GROUPS)

#define INODE_SIZE 128
#define SECTORS_PER_BLOCK (MAX_BLOCK_SIZE / MAX_SECTOR_SIZE)
#define INODES_PER_BLOCK (MAX_BLOCK_SIZE / INODE_SIZE)
#define ENTRY_PER_BLOCK (MAX_BLOCK_SIZE / sizeof(EXT2_DIR_ENTRY))

#define USER_PERMISSION 0x1FFu
#define FILE_TYPE_MASK 0xF000u
#define FILE_TYPE_DIR 0x4000u
#define FILE_TYPE_FILE 0x8000u

#define EXT2_MAGIC 0xEF53

#define DIR_ENTRY_FREE 0x01
#define DIR_ENTRY_NO_MORE 0x00

#define RESERVED_INODE_NUM 10
#define FIRST_NON_RESERVED_INODE 11
#define NUM_OF_ROOT_INODE 2

#define SPACE_CHAR 0x20

/* Layout of every group, as block offsets from the start of the group. */
#define BOOT_BLOCK_BASE 0
#define SUPER_BLOCK_BASE (BOOT_BLOCK_BASE + 1)
#define GDT_BASE (SUPER_BLOCK_BASE + 1)
#define BLOCK_NUM_GDT_PER_GROUP ((32 * NUMBER_OF_GROUPS + (MAX_BLOCK_SIZE - 1)) / MAX_BLOCK_SIZE)
#define BLOCK_BITMAP_BASE (GDT_BASE + BLOCK_NUM_GDT_PER_GROUP)
#define INODE_BITMAP_BASE (BLOCK_BITMAP_BASE + 1)
#define INODE_TABLE_BASE (INODE_BITMAP_BASE + 1)
#define BLOCK_NUM_IT_PER_GROUP ((INODE_SIZE * INODES_PER_GROUP + (MAX_BLOCK_SIZE - 1)) / MAX_BLOCK_SIZE)
#define DATA_BLOCK_BASE (INODE_TABLE_BASE + BLOCK_NUM_IT_PER_GROUP)

#define GET_INODE_GROUP(x) (((x) - 1) / INODES_PER_GROUP)
#define GET_DATA_GROUP(x) ((x) / BLOCKS_PER_GROUP)

#pragma pack(push, 1)

typedef struct {
    UINT32 max_inode_count;
    UINT32 block_count;
    UINT32 reserved_block_count;
    UINT32 free_block_count;
    UINT32 free_inode_count;
    UINT32 first_data_block;
    UINT32 log_block_size;
    UINT32 log_fragmentation_size;
    UINT32 block_per_group;
    UINT32 fragmentation_per_group;
    UINT32 inode_per_group;
    UINT32 mtime;
    UINT32 wtime;
    UINT16 mount_count;
    UINT16 max_mount_count;
    UINT16 magic_signature;
    UINT16 state;
    UINT16 errors;
    UINT16 minor_version;
    UINT32 last_consistency_check_time;
    UINT32 check_interval;
    UINT32 creator_os;
    UINT16 UID_that_can_use_reserved_blocks;
    UINT16 GID_that_can_use_reserved_blocks;
    UINT32 first_non_reserved_inode;
    UINT16 inode_structure_size;
    UINT16 block_group_number;
    UINT32 compatible_feature_flags;
    UINT32 incompatible_feature_flags;
    UINT32 read_only_feature_flags;
    UINT32 UUID[4];
    UINT32 volume_name[4];
    UINT32 last_mounted_path[16];
    UINT32 algorithm_usage_bitmap;
    UINT8 preallocated_blocks_count;
    UINT8 preallocated_dir_blocks_count;
    BYTE padding[2];
    UINT32 journal_UUID[4];
    UINT32 journal_inode_number;
    UINT32 journal_device;
    UINT32 orphan_inode_list;
    UINT32 hash_seed[4];
    UINT8 defined_hash_version;
    BYTE padding1;
    BYTE padding2[2];
    UINT32 default_mount_option;
    UINT32 first_data_block_each_group;
    BYTE reserved[764];
} EXT2_SUPER_BLOCK;

typedef struct {
    UINT32 start_block_of_block_bitmap;
    UINT32 start_block_of_inode_bitmap;
    UINT32 start_block_of_inode_table;
    UINT16 free_blocks_count;
    UINT16 free_inodes_count;
    UINT16 directories_count;
    BYTE padding[2];
    BYTE reserved[12];
} EXT2_GROUP_DESCRIPTOR;

typedef struct {
    UINT16 mode;
    UINT16 uid;
    UINT32 size;
    UINT32 atime;
    UINT32 ctime;
    UINT32 mtime;
    UINT32 dtime;
    UINT16 gid;
    UINT16 links_count;
    UINT32 blocks;
    UINT32 flags;
    UINT32 i_reserved1;
    UINT32 block[EXT2_N_BLOCKS];
    UINT32 generation;
    UINT32 file_acl;
    UINT32 dir_acl;
    UINT32 faddr;
    UINT32 i_reserved2[3];
} INODE;

/* Fixed-size entry with an 8.3 name, unlike real ext2's variable-length entries. */
typedef struct {
    UINT32 inode;
    char name[MAX_ENTRY_NAME_LENGTH];
    UINT32 name_len;
    BYTE pad[13];
} EXT2_DIR_ENTRY;

#pragma pack(pop)

static_assert(sizeof(EXT2_SUPER_BLOCK) == MAX_BLOCK_SIZE, "superblock must fill one block");
static_assert(sizeof(EXT2_GROUP_DESCRIPTOR) == 32, "group descriptor is 32 bytes");
static_assert(sizeof(INODE) == INODE_SIZE, "inode is 128 bytes");
static_assert(sizeof(EXT2_DIR_ENTRY) == 32, "directory entry is 32 bytes");
static_assert(DATA_BLOCK_BASE < BLOCKS_PER_GROUP, "group metadata must leave room for data");

#endif
