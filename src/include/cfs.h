#ifndef CFS_CFS_H
#define CFS_CFS_H

#include <cstdint>
#include <vector>
#include <sys/stat.h>

namespace cfs
{
    struct stat {
        dev_t       st_dev;         /* ID of device containing file */
        ino_t       st_ino;         /* Inode number */
        mode_t      st_mode;        /* File type and mode */
        char _reserved_[4];
        nlink_t     st_nlink;       /* Number of hard links */
        uid_t       st_uid;         /* User ID of owner */
        gid_t       st_gid;         /* Group ID of owner */
        dev_t       st_rdev;        /* Device ID (if special file) */
        off_t       st_size;        /* Total size, in bytes */
        blksize_t   st_blksize;     /* Block size for filesystem I/O */
        blkcnt_t    st_blocks;      /* Number of 512 B blocks allocated */
        timespec    st_atim;        /* Time of last access */
        timespec    st_mtim;        /* Time of last modification */
        timespec    st_ctim;        /* Time of last status change */
    };
    constexpr uint64_t stat_size = 120;
    static_assert(sizeof(stat) == stat_size);

    constexpr uint64_t cfs_magick_number = 0xCFADBEEF20251216;
    constexpr uint64_t cfs_magic_number_compliment = ~cfs_magick_number;
    constexpr uint64_t cfs_header_size = 512;

    constexpr auto cfs_implementation_version = CFS_IMPLEMENT_VERSION;
    constexpr auto cfs_standard_version = CFS_STANDARD_VERSION;

    struct cfs_head_t
    {
        uint64_t magick; // fs magic
        struct static_info_t {
            char label [64];
            uint64_t block_size;
            uint64_t blocks; // block numbers
            uint64_t data_bitmap_start;
            uint64_t data_bitmap_end;
            uint64_t data_bitmap_backup_start;
            uint64_t data_bitmap_backup_end;
            uint64_t data_block_attribute_table_start; // attribute is 16 byte for each data block
            uint64_t data_block_attribute_table_end;
            uint64_t data_table_start;
            uint64_t data_table_end;
            uint64_t journal_start;
            uint64_t journal_end;
        };

        static_info_t static_info; // static info
        static_info_t static_info_dup; // static info, dup
        uint64_t static_info_checksum; // static info checksum
        uint64_t static_info_checksum_dup; // static info checksum, dup

        struct runtime_info_t {
            uint64_t mount_timestamp;       // when was the last time it's mounted
            uint64_t last_check_timestamp;  // last time check ran
            struct {
                uint64_t clean:1;
                uint64_t _reserved:63;
            } flags;
            uint64_t last_allocated_block;
            uint64_t allocated_non_cow_blocks;
            uint64_t root_inode_pointer;
        };
        runtime_info_t runtime_info; // runtime info
        runtime_info_t runtime_info_cow; // cow of the last change
        uint64_t runtime_info_checksum; // runtime info
        uint64_t runtime_info_checksum_cow; // crc64 of cow of the last change

        struct {
            uint64_t _1;
            uint64_t _2;
            uint64_t _3;
            uint64_t _4;
            uint64_t _5;
            uint64_t _6;
            uint64_t _7;
        } _reserved_;
    };
    static_assert(sizeof(cfs_head_t) == cfs_header_size, "Faulty header size");
    constexpr uint64_t cfs_minimum_size = 1024 * 1024 * 1;

    enum BlockStatusType : uint8_t {
        BLOCK_AVAILABLE_TO_MODIFY_0x00 = 0x00,
        BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01 = 0x01,
        BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02 = 0x02,
        BlockStatusType_reserved_0x03 = 0x03,
    };

    enum BlockType : uint8_t {
        COW_REDUNDANCY_BLOCK = 0x00,
        INDEX_NODE_BLOCK = 0x01,
        POINTER_BLOCK = 0x02,
        STORAGE_BLOCK = 0x03,
    };

    enum BlockAttribute_BlockType : uint8_t {
        CowRedundancy = 0x00,
        IndexNode = 0x01,
        PointerBlock = 0x02,
        StorageBlock = 0x03
    };

    constexpr uint64_t cfs_block_attribute_size = 4;
    struct cfs_block_attribute_t {
        uint32_t block_status:2;    // => BlockStatusType
        uint32_t block_type:2;      // => BlockType
        uint32_t block_type_cow:2;
        uint32_t allocation_oom_scan_per_refresh_count:4;
        uint32_t index_node_referencing_number:17; // Max 0x1FFFF depth of snapshots
        uint32_t block_checksum:5;
    };
    static_assert(sizeof(cfs_block_attribute_t) == cfs_block_attribute_size, "Faulty attribute size");

    constexpr uint64_t cfs_journal_action_size = 64;
    struct cfs_action_t
    {
        uint64_t cfs_magic;
        uint64_t action_param_crc64;
        union
        {
            struct action_plain_t
            {
                uint64_t action;
                uint64_t action_param0;
                uint64_t action_param1;
                uint64_t action_param2;
                uint64_t action_param3;
                uint64_t action_param4;
            } action_plain;
        } action_data;
    };
    static_assert(sizeof(cfs_action_t) == cfs_journal_action_size, "Faulty journal action size");

    constexpr uint64_t cfs_journal_header_size = 32;
    struct journal_header_t {
        uint64_t head;
        uint64_t tail;
        uint64_t size;
        uint64_t reserved;
    };
    static_assert(sizeof(journal_header_t) == cfs_journal_header_size, "Faulty journal header size");

#   define _CFS_str(x) #x
#   define CFS_str(x) _CFS_str(x)
#   define FilesystemActionType_Def(name, val) \
    enum FilesystemActionType_##name : uint64_t { name = val }; \
    constexpr const char * name##_c_str = CFS_str(name);

    FilesystemActionType_Def(CorruptionDetected, 0x2000); // [Corruption Type]
    FilesystemActionType_Def(BitmapMirrorInconsistent, 0x2001);
    FilesystemActionType_Def(FilesystemBlockExhausted, 0x2002);

    FilesystemActionType_Def(AttemptedFixFinishedAndAssumedFine, 0x2020); // [Corruption Type]

    FilesystemActionType_Def(GlobalTransaction, 0x3000) // [Transaction Type], [PARAM...]

#   define GlobalTransaction_Def(x, val) \
    FilesystemActionType_Def(x, val); \
    FilesystemActionType_Def(x##_Completed, val + 1); \
    FilesystemActionType_Def(x##_Failed, val + 2);

    GlobalTransaction_Def(FilesystemBitmapModification,         0x2010);    // [FROM] [TO] [LOCATION]
    GlobalTransaction_Def(GlobalTransaction_AllocateBlock,      0x3001)
    GlobalTransaction_Def(GlobalTransaction_DeallocateBlock,    0x3004)     // [Where]
    GlobalTransaction_Def(GlobalTransaction_CreateRedundancy,   0x3007)     // [Which] [Where]

    // major change, which are write inode, inode metadata modification, or snapshot creation/revert/deletion
    GlobalTransaction_Def(GlobalTransaction_Major_WriteInode,         0x300A)     // [Which inode] [Offset] [Size]
    GlobalTransaction_Def(GlobalTransaction_Major_InodeMetadataModification, 0x300D)     // [Which inode] [CoW Pointer]
    GlobalTransaction_Def(GlobalTransaction_Major_SnapshotCreation,   0x3010)
    GlobalTransaction_Def(GlobalTransaction_Major_SnapshotRevert,     0x3013)     // [Version Entry Point]
    GlobalTransaction_Def(GlobalTransaction_Major_SnapshotDeletion,   0x3016)     // [Version Entry Point]
    /// CFS inode memory mapper
    class cfs_inode_t {
        char * data_ = nullptr;

    public:
        stat * cfs_inode_attribute = nullptr;
        uint64_t * cfs_level_1_indexes = nullptr;
        const uint64_t cfs_level_1_index_numbers = 0;

        void convert(char * data, const uint64_t block_size)
        {
            *const_cast<uint64_t *>(&cfs_level_1_index_numbers) = ((block_size - sizeof(stat)) / sizeof(uint64_t));
            data_ = data;
            cfs_inode_attribute = reinterpret_cast<stat *>(data);
            cfs_level_1_indexes = reinterpret_cast<uint64_t *>(data + sizeof(stat));
        }
    };

    struct dentry_static_metadata_section_t {

    };

}

#endif //CFS_CFS_H