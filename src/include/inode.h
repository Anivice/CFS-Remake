#ifndef CFS_INODE_H
#define CFS_INODE_H

#include "cfsBasicComponents.h"
#include "tsl/hopscotch_map.h"
#include "tsl/bhopscotch_map.h"

namespace cfs
{
    class dentry_t; /// directory entry

    /// inode
    class inode_t {
    private:
        struct {
            filesystem * parent_fs_governor;
            cfs_block_manager_t * block_manager;
            cfs_journaling_t * journal;
            cfs_block_attribute_access_t * block_attribute;
        } inode_construct_info_;

        inode_t * parent_inode_; /// parent
        std::unique_ptr < cfs_inode_service_t > referenced_inode_; /// referenced inode
        uint64_t current_referenced_inode_; /// referenced inode number
        const decltype(inode_construct_info_.parent_fs_governor->static_info_) * static_info_; /// static info so I don't have to type

        std::mutex dentry_map_mutex_; /// dentry map mutex lock
        tsl::hopscotch_map<std::string, uint64_t> dentry_map_; /// name -> inode dentry
        tsl::hopscotch_map<uint64_t, std::string> dentry_map_reversed_search_map_; /// reversed search map, inode -> name
        uint64_t dentry_start_ = 0; /// dentry info offset, for root metadata jump

        int inode_type() const { return referenced_inode_->get_stat().st_mode & S_IFMT; } // POSIX types

        // [DENTRY SIGNARURE] -> cfs_magick_number
        // LZ4 Compression size [int]
        // LZ4 Compressed data: [STRING]'0'[64BIT INODE]

        /// save dentry map to disk
        void save_dentry_unblocked();

        /// read dentry from the disk
        void read_dentry_unblocked();

        /// dump raw inode data as std::vector <uint8_t>
        /// @return Raw inode data as std::vector<uint8_t>, size is block size
        std::vector<uint8_t> dump_inode_raw() const;

        /// CoW. if CoW happened, current inode reference will be changed automatically
        void copy_on_write();

    public:
        /// CoW on dentry level.
        /// Changes will be reflected from child to parent one by one until it reaches root
        /// where it effectively saves all fs info before any changes occur, thus snapshots can have their
        /// own overview of the while filesystem.
        /// This is from WAFL (Write Anywhere File Layout), a 90s UNIX NFS (Network File System) where
        /// a concept called "per-snapshot bitmap" was firstly introduced.
        /// More info: Dave Hitz, James Lau, and Michael Malcolm: File System Design for an NFS File Server Appliance, 1994
        /// (https://www.netapp.com/media/23880-file-system-design.pdf)
        /// @param cow_index Current child inode index
        /// @param content Inode content for CoW. Parent cannot lock inode when it's in use so
        /// @return New inode index, you should immediately switch your reference if inode index changes
        uint64_t inode_copy_on_write(uint64_t cow_index, const std::vector<uint8_t> & content);

    public:
        /// Create an inode
        /// @param index Inode index
        /// @param parent_fs_governor
        /// @param block_manager
        /// @param journal
        /// @param block_attribute
        /// @param parent_inode Parent inode, always dentry_t
        inode_t(
            uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            inode_t * parent_inode);

        /// Resize inode
        /// @param size New size
        void resize(uint64_t size);

        /// Read inode data
        /// @param data dest
        /// @param size read size
        /// @param offset read offset
        /// @return size read
        uint64_t read(char * data, uint64_t size, uint64_t offset) const;

        /// write to inode data.
        /// write automatically resizes when offset+size > st_size, but will not shrink.
        /// you have to call resize(0) to shrink the inode
        /// @param data src
        /// @param size write size
        /// @param offset write offset
        /// @return size written
        uint64_t write(const char * data, uint64_t size, uint64_t offset);
    };

    class dentry_t : public inode_t {
    public:
    };
}

#endif //CFS_INODE_H