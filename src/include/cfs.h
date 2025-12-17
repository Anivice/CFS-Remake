#ifndef CFS_CFS_H
#define CFS_CFS_H

#include <cstdint>

namespace cfs
{
    constexpr uint64_t cfs_magick_number = 0xCFADBEEF20251216;
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
            uint64_t snapshot_number;
            uint64_t snapshot_number_cow;
            uint64_t allocation_bitmap_checksum;
            uint64_t allocation_bitmap_checksum_cow;
            struct {
                uint64_t clean:1;
            } flags;
            uint64_t last_allocated_block;
            uint64_t allocated_blocks;
        };
        runtime_info_t runtime_info; // runtime info
        runtime_info_t runtime_info_cow; // cow of the last change
        uint64_t runtime_info_checksum; // runtime info
        uint64_t runtime_info_checksum_cow; // crc64 of cow of the last change

        struct {
            uint64_t _1;
        } _reserved_;
    };
    static_assert(sizeof(cfs_head_t) == cfs_header_size, "Faulty header size");
    constexpr uint64_t cfs_minimum_size = 1024 * 1024 * 1;

    enum BlockStatusType : uint8_t {
        BLOCK_AVAILABLE_TO_MODIFY_0x00 = 0x00,
        BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01 = 0x01,
        BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK = 0x02,
        BlockStatusType_reserved_0x03 = 0x03,
    };

    enum BlockType : uint8_t {
        COW_REDUNDANCY_BLOCK = 0x00,
        INDEX_NODE_BLOCK = 0x01,
        POINTER_BLOCK = 0x02,
        STORAGE_BLOCK = 0x03,
    };

    constexpr uint64_t cfs_block_attribute_size = 4;
    struct cfs_block_attribute_t {
        uint32_t block_status:2;    // => BlockStatusType
        uint32_t block_type:2;      // => BlockType
        uint32_t block_type_cow:2;
        uint32_t allocation_oom_scan_per_refresh_count:4;
        uint32_t newly_allocated_thus_no_cow:1;
        uint32_t index_node_referencing_number:16; // Max 0xFFFF (65535) depth of snapshots
        uint32_t _reserved_:5;
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
}

#endif //CFS_CFS_H