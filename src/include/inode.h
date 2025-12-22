#ifndef CFS_INODE_H
#define CFS_INODE_H

#include "cfsBasicComponents.h"

namespace cfs
{
    class dentry_t; /// directory entry

    /// inode
    class inode_t {
    private:
        inode_t * parent_inode_;
        std::unique_ptr < cfs_inode_service_t > referenced_inode_;
        uint64_t current_referenced_inode_;

    public:
        /// CoW on dentry level
        /// @param cow_index Current child inode index
        /// @return New inode index, you should immediately switch your reference if inode index changes
        uint64_t inode_copy_on_write(uint64_t cow_index)
        {
            if (parent_inode_ == nullptr) // Root inode, write inode into metadata dentry section
            {
                // [BITMAP]
                // [ATTRIBUTES]
                // [INODE]
                // [STATIC DATA]
                // [DENTRY ENTRY]

                const auto bitmap_dump = referenced_inode_->block_manager_->dump_bitmap_data();
                const auto attribute_dump = referenced_inode_->block_attribute_->dump();
                std::vector<uint8_t> inode_metadata(referenced_inode_->block_size_);
                std::memcpy(inode_metadata.data(), referenced_inode_->inode_effective_lock_.data(),
                    referenced_inode_->inode_effective_lock_.size());
                /// no static data, yet

                std::vector<uint8_t> data(bitmap_dump.size() + attribute_dump.size() + inode_metadata.size());

                uint64_t offset;
                auto write_to_buffer = [&](const std::vector<uint8_t> & buffer) {
                    std::memcpy(data.data() + offset, buffer.data(), buffer.size());
                    offset += buffer.size();
                };

                write_to_buffer(bitmap_dump);
                write_to_buffer(attribute_dump);
                write_to_buffer(inode_metadata);

                const auto data_compressed = utils::arithmetic::compress(data);
                int size = *(int*)(data_compressed.data() + data_compressed.size() - sizeof(int)); // size is at the end of the stream
                // we need to move it to the front
                referenced_inode_->write((char*)&size, sizeof(size), 0);
                referenced_inode_->write((char*)data_compressed.data(), data_compressed.size() - sizeof(int), sizeof(int));
                /// Now, we have written all the metadata
            }

            /// TODO: check if CoW is required
            /// TODO: Allocate a new inode, copy old data over, set corresponding flags on both new and old inodes
            /// TODO: rewrite all the dentry information, including relink

            return cow_index;
        }

    public:
        /// Create an inode
        /// @param index Inode index
        /// @param parent_fs_governor
        /// @param block_manager
        /// @param journal
        /// @param block_attribute
        /// @param parent_inode Parent inode, always dentry_t
        inode_t(
            const uint64_t index,
            filesystem * parent_fs_governor,
            cfs_block_manager_t * block_manager,
            cfs_journaling_t * journal,
            cfs_block_attribute_access_t * block_attribute,
            inode_t * parent_inode) : parent_inode_(parent_inode)
        {
            current_referenced_inode_ = index;
            referenced_inode_ = std::make_unique<cfs_inode_service_t>(index, parent_fs_governor, block_manager, journal, block_attribute);
        }

        void resize(const uint64_t size)
        {
            if (parent_inode_) parent_inode_->inode_copy_on_write();
            referenced_inode_->resize(size);
        }

        /// Read inode data
        /// @param data dest
        /// @param size read size
        /// @param offset read offset
        /// @return size read
        uint64_t read(char * data, const uint64_t size, const uint64_t offset) {
            return referenced_inode_->read(data, size, offset);
        }

        /// write to inode data
        /// write automatically resizes when offset+size > st_size, but will not shrink
        /// you have to call resize(0) to shrink the inode
        /// @param data src
        /// @param size write size
        /// @param offset write offset
        /// @return size written
        uint64_t write(const char * data, const uint64_t size, const uint64_t offset) {
            return referenced_inode_->write(data, size, offset);
        }
    };

    class dentry_t : public inode_t {
    public:
    };
}

#endif //CFS_INODE_H