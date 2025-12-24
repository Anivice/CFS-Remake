#ifndef CFS_INODE_H
#define CFS_INODE_H

#include "cfsBasicComponents.h"
#include "tsl/hopscotch_map.h"
#include "tsl/bhopscotch_map.h"

make_simple_error_class(cannot_initialize_dentry_on_non_dentry_inodes)
make_simple_error_class(cannot_initialize_non_dentry_on_dentry_inodes)

namespace cfs
{
    class dentry_t; /// directory entry

    /// inode
    class inode_t {
    protected:
        struct {
            filesystem * parent_fs_governor;
            cfs_block_manager_t * block_manager;
            cfs_journaling_t * journal;
            cfs_block_attribute_access_t * block_attribute;
        } inode_construct_info_ { };

        inode_t * parent_inode_; /// parent
        std::unique_ptr < cfs_inode_service_t > referenced_inode_; /// referenced inode
        uint64_t current_referenced_inode_; /// referenced inode number
        const decltype(inode_construct_info_.parent_fs_governor->static_info_) * static_info_; /// static info so I don't have to type
        uint64_t dentry_start_ = 0; /// dentry info offset, for root metadata jump

        std::mutex operation_mutex_;
        /* tsl::hopscotch_map */ std::unordered_map <std::string, uint64_t> dentry_map_; /// name -> inode dentry
        /* tsl::hopscotch_map */ std::unordered_map <uint64_t, std::string> dentry_map_reversed_search_map_; /// reversed search map, inode -> name

        [[nodiscard]] mode_t inode_type() const { return referenced_inode_->get_stat().st_mode & S_IFMT; } // POSIX types

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

        /// CoW on dentry level.
        /// Changes will be reflected from child to parent one by one until it reaches root
        /// where it effectively saves all fs info before any changes occur, thus snapshots can have their
        /// own overview of the while filesystem.
        /// This is from WAFL (Write Anywhere File Layout), a 90s UNIX NFS (Network File System) where
        /// a concept called "per-snapshot bitmap" was firstly introduced.
        /// More info: Dave Hitz, James Lau, and Michael Malcolm: File System Design for an NFS File Server Appliance, 1994
        /// (https://www.netapp.com/media/23880-file-system-design.pdf)
        void root_cow();

        /// @param cow_index Current child inode index
        /// @param content Inode content for CoW. Parent cannot lock inode when it's in use so
        /// @return New inode index, you should immediately switch your reference if inode index changes
        uint64_t copy_on_write_invoked_from_child(uint64_t cow_index, const std::vector<uint8_t> & content);

    public:
        NO_COPY_OBJ(inode_t)

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
        virtual uint64_t read(char * data, uint64_t size, uint64_t offset);

        /// write to inode data.
        /// write automatically resizes when offset+size > st_size, but will not shrink.
        /// you have to call resize(0) to shrink the inode
        /// @param data src
        /// @param size write size
        /// @param offset write offset
        /// @return size written
        virtual uint64_t write(const char * data, uint64_t size, uint64_t offset);

        void chdev(dev_t dev);                // change st_dev
        void chrdev(dev_t dev);             // change st_rdev
        void chmod(mode_t mode);               // change st_mode
        void chown(uid_t uid, gid_t gid);       // change st_uid, st_gid
        void set_atime(timespec st_atim);   // change st_atim
        void set_ctime(timespec st_ctim);   // change st_ctim
        void set_mtime(timespec st_mtim);   // change st_mtim

        /// get struct stat
        [[nodiscard]] struct stat get_stat() const { return this->referenced_inode_->get_stat(); }

        /// Return inode content size
        uint64_t size() const { return referenced_inode_->cfs_inode_attribute->st_size; }

        virtual ~inode_t() = default;
    };

    class file_t : public inode_t {
    public:
        NO_COPY_OBJ(file_t)

        /// Create a non-dentry inode
        /// @param index Inode index
        /// @param parent_fs_governor
        /// @param block_manager
        /// @param journal
        /// @param block_attribute
        /// @param parent_inode Parent inode, always dentry_t
        file_t(
            const uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            inode_t * parent_inode)
        : inode_t(index, parent_fs_governor, block_manager, journal, block_attribute, parent_inode)
        {
            if (inode_t::inode_type() == S_IFDIR) {
                throw error::cannot_initialize_non_dentry_on_dentry_inodes();
            }
        }
    };

    class dentry_t : public inode_t {
    public:
        NO_COPY_OBJ(dentry_t)

        /// Create a non-dentry inode
        /// @param index Inode index
        /// @param parent_fs_governor
        /// @param block_manager
        /// @param journal
        /// @param block_attribute
        /// @param parent_inode Parent inode, always dentry_t
        dentry_t(
            const uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            inode_t * parent_inode)
        : inode_t(index, parent_fs_governor, block_manager, journal, block_attribute, parent_inode)
        {
            if (inode_type() != S_IFDIR) {
                throw error::cannot_initialize_dentry_on_non_dentry_inodes();
            }
            std::lock_guard lock(operation_mutex_);
            read_dentry_unblocked();
        }

        using dentry_pairs_t = tsl::hopscotch_map<std::string, uint64_t>;

        /// List all dentries under current dentry_t
        dentry_pairs_t ls();

        /// unlink one inode
        /// @param name Inode name
        void unlink(const std::string & name);

        uint64_t erase_entry(const std::string & name)
        {
            std::lock_guard lock(operation_mutex_);
            const auto ptr = dentry_map_.find(name);
            cfs_assert_simple (ptr != dentry_map_.end());
            const auto inode = ptr->second;
            dentry_map_.erase(ptr);
            dentry_map_reversed_search_map_.erase(inode);
            save_dentry_unblocked();
            return inode;
        }

        void add_entry(const std::string & name, const uint64_t index)
        {
            std::lock_guard lock(operation_mutex_);
            const auto ptr = dentry_map_.find(name);
            cfs_assert_simple (ptr == dentry_map_.end());
            dentry_map_.emplace(name, index);
            dentry_map_reversed_search_map_.emplace(index, name);
            save_dentry_unblocked();
        }

        /// create an inode under current dentry
        /// @param name Inode dentry name
        /// @return New inode
        template < class InodeType >
        InodeType make_inode(const std::string & name)
        {
            std::lock_guard<std::mutex> lock(operation_mutex_);
            const auto ptr = dentry_map_.find(name);
            cfs_assert_simple (ptr == dentry_map_.end());
            copy_on_write(); // relink
            const auto new_index = inode_construct_info_.block_manager->allocate();
            // clear inode data
            {
                inode_construct_info_.block_attribute->clear(new_index, {
                    .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
                    .block_type = INDEX_NODE_BLOCK,
                    .block_type_cow = 0,
                    .allocation_oom_scan_per_refresh_count = 0,
                    .newly_allocated_thus_no_cow = 0,
                    .index_node_referencing_number = 1,
                    .block_checksum = 0
                });
                const auto new_lock = inode_construct_info_.parent_fs_governor->lock(new_index + static_info_->data_table_start);
                std::memset(new_lock.data(), 0, new_lock.size());
                const auto now = utils::get_timespec();
                struct stat inode_stat{};
                if constexpr (std::is_same_v<InodeType, dentry_t>)
                {
                    inode_stat = {
                        .st_dev = 0,
                        .st_ino = new_index,
                        .st_nlink = 1,
                        .st_mode = S_IFDIR | 0755,
                        .st_uid = getuid(),
                        .st_gid = getgid(),
                        .st_rdev = 0,
                        .st_size = 0,
                        .st_blksize = static_cast<decltype(inode_stat.st_blksize)>(static_info_->block_size),
                        .st_blocks = 0,
                        .st_atim = now,
                        .st_mtim = now,
                        .st_ctim = now,
                    };
                }
                else if constexpr (std::is_same_v<InodeType, file_t>) {
                    inode_stat = {
                        .st_dev = 0,
                        .st_ino = new_index,
                        .st_nlink = 1,
                        .st_mode = S_IFREG | 0755,
                        .st_uid = getuid(),
                        .st_gid = getgid(),
                        .st_rdev = 0,
                        .st_size = 0,
                        .st_blksize = static_cast<decltype(inode_stat.st_blksize)>(static_info_->block_size),
                        .st_blocks = 0,
                        .st_atim = now,
                        .st_mtim = now,
                        .st_ctim = now,
                    };
                }
                else if constexpr (std::is_same_v<InodeType, inode_t>) {
                    inode_stat = {
                        .st_dev = 0,
                        .st_ino = new_index,
                        .st_nlink = 1,
                        .st_mode = S_IFREG | 0755,
                        .st_uid = getuid(),
                        .st_gid = getgid(),
                        .st_rdev = 0,
                        .st_size = 0,
                        .st_blksize = static_cast<decltype(inode_stat.st_blksize)>(static_info_->block_size),
                        .st_blocks = 0,
                        .st_atim = now,
                        .st_mtim = now,
                        .st_ctim = now,
                    };
                }
                std::memcpy(new_lock.data(), &inode_stat, sizeof(inode_stat)); // new inode struct stat
            }

            this->dentry_map_.emplace(name, new_index);
            this->dentry_map_reversed_search_map_.emplace(new_index, name);
            save_dentry_unblocked();

            return InodeType(new_index,
                    inode_construct_info_.parent_fs_governor, inode_construct_info_.block_manager,
                    inode_construct_info_.journal, inode_construct_info_.block_attribute,
                    this);
        }
    };
}

#endif //CFS_INODE_H