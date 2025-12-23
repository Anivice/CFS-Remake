#include "inode.h"

void cfs::inode_t::save_dentry_unblocked()
{
    std::vector<uint8_t> buffer_;
    buffer_.clear();
    buffer_.reserve(dentry_map_.size() * 32);

    auto save_bytes = [&](const auto data_type) {
        for (int i = 0; i < sizeof(data_type); i++) { // dentry start signature
            buffer_.push_back(((char*)&data_type)[i]);
        }
    };

    for (const auto [string, inode] : dentry_map_)
    {
        for (const auto c : string) { buffer_.push_back(c); }
        buffer_.push_back(0); // null terminated string TODO: DO NOT ALLOW ANY CREATION OF NULL TERMINATED DENTRY NAME
        save_bytes(inode);
    }

    auto compressed = utils::arithmetic::compress(buffer_);
    const int size = *(int*)(compressed.data() + compressed.size() - sizeof(int));
    compressed.resize(compressed.size() - sizeof(int));
    buffer_.clear();
    buffer_.reserve(size + sizeof(cfs_magick_number) + sizeof(int));
    /// Write dentry data
    save_bytes(cfs_magick_number);
    save_bytes(size);
    buffer_.insert(buffer_.end(), compressed.begin(), compressed.end());
    referenced_inode_->write((char*)buffer_.data(), buffer_.size(), dentry_start_);
}

void cfs::inode_t::read_dentry_unblocked()
{
    std::vector<uint8_t> buffer_(size(), 0);
    referenced_inode_->read((char*)buffer_.data(), buffer_.size(), 0);
    dentry_map_.clear();
    dentry_map_reversed_search_map_.clear();
    if (buffer_.size() < sizeof(uint64_t)) {
        return;
    }

    const uint64_t * magic = (uint64_t*)buffer_.data();
    for (uint64_t i = 0; i < (buffer_.size() - 8); i++)
    {
        if (*magic == cfs_magick_number) {
            dentry_start_ = i;
            break;
        }
    }

    // decompress expects size to be at the end of the stream
    const auto cSize = buffer_.begin() + dentry_start_ + sizeof(cfs_magick_number);
    std::vector dentry_data_(cSize + sizeof(int), buffer_.end());
    dentry_data_.insert(dentry_data_.end(), cSize, cSize + sizeof(int));

    // decompress data
    const auto decompressed = utils::arithmetic::decompress(dentry_data_);

    std::string name;
    uint64_t ptr;
    int offset = 0;
    bool name_is_true_ptr_is_false = true;
    std::ranges::for_each(decompressed, [&](const char c)
    {
        if (name_is_true_ptr_is_false)
        {
            if (c == 0) {
                name_is_true_ptr_is_false = false;
                return;
            }

            name += c;
        }
        else
        {
            ((char*)(&ptr))[offset++] = c;
            if (offset == sizeof(uint64_t))
            {
                dentry_map_.emplace(name, ptr);
                dentry_map_reversed_search_map_.emplace(ptr, name);
                name_is_true_ptr_is_false = true;
            }
        }
    });
}

std::vector<uint8_t> cfs::inode_t::dump_inode_raw() const
{
    std::vector<uint8_t> my_data;
    my_data.resize(static_info_->block_size); // inode metadata size
    std::memcpy(my_data.data(), referenced_inode_->inode_effective_lock_.data(), my_data.size());
    return my_data;
}

void cfs::inode_t::copy_on_write()
{
    const std::vector<uint8_t> my_data = dump_inode_raw();
    const auto new_inode_num_ = parent_inode_->inode_copy_on_write(current_referenced_inode_, my_data); // upload
    if (new_inode_num_ != current_referenced_inode_) // I got referenced
    {
        const auto old_ = current_referenced_inode_;
        current_referenced_inode_ = new_inode_num_; // get new inode
        referenced_inode_ = std::make_unique<cfs_inode_service_t>(new_inode_num_,
                                                                  inode_construct_info_.parent_fs_governor,
                                                                  inode_construct_info_.block_manager,
                                                                  inode_construct_info_.journal,
                                                                  inode_construct_info_.block_attribute); // relocate reference
        inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
        inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
    }
}

uint64_t cfs::inode_t::inode_copy_on_write(const uint64_t cow_index, const std::vector<uint8_t> &content)
{
    std::lock_guard lock(operation_mutex_);
    std::vector<uint8_t> data_;
    if (parent_inode_ == nullptr) // Root inode, write inode into metadata dentry section
    {
        // create a new block
        const auto new_inode_num_ = inode_construct_info_.block_manager->allocate();
        // set attributes
        inode_construct_info_.block_attribute->clear(new_inode_num_, {
                                                         .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
                                                         .block_type = INDEX_NODE_BLOCK,
                                                         .block_type_cow = 0,
                                                         .allocation_oom_scan_per_refresh_count = 0,
                                                         .newly_allocated_thus_no_cow = 0,
                                                         .index_node_referencing_number = 1,
                                                         .block_checksum = 0,
                                                     });

        /// dump my own data to new inode
        {
            const auto new_lock = inode_construct_info_.parent_fs_governor->
                    lock(new_inode_num_ + static_info_->data_table_start);
            std::memcpy(new_lock.data(), referenced_inode_->inode_effective_lock_.data(), static_info_->block_size);
        }

        const auto old_ = current_referenced_inode_;
        current_referenced_inode_ = new_inode_num_; // get new inode
        referenced_inode_ = std::make_unique<cfs_inode_service_t>(new_inode_num_,
                                                                  inode_construct_info_.parent_fs_governor,
                                                                  inode_construct_info_.block_manager,
                                                                  inode_construct_info_.journal,
                                                                  inode_construct_info_.block_attribute); // relocate

        ////////////////////////////////////////////////////////////////////////////////////////////////////////
        /////////////  _    _______ _____ _____ _____  ______ _____   _____ _   _ ______ _____    //////////////
        ///////////// | |  | | ___ \_   _|_   _|  ___| |  ___/  ___| |_   _| \ | ||  ___|  _  |   //////////////
        ///////////// | |  | | |_/ / | |   | | | |__   | |_  \ `--.    | | |  \| || |_  | | | |   //////////////
        ///////////// | |/\| |    /  | |   | | |  __|  |  _|  `--. \   | | | . ` ||  _| | | | |   //////////////
        ///////////// \  /\  / |\ \ _| |_  | | | |___  | |   /\__/ /  _| |_| |\  || |   \ \_/ /   //////////////
        /////////////  \/  \/\_| \_|\___/  \_/ \____/  \_|   \____/   \___/\_| \_/\_|    \___/    //////////////
        ////////////////////////////////////////////////////////////////////////////////////////////////////////

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

        data_.resize(bitmap_dump.size() + attribute_dump.size() + inode_metadata.size()); // we keep this buffer
        /// so that less malloc is called
                /// data will be overwritten anyway so

        uint64_t offset = 0;
        auto write_to_buffer = [&](const std::vector<uint8_t> & buffer) {
            std::memcpy(data_.data() + offset, buffer.data(), buffer.size());
            offset += buffer.size();
        };

        write_to_buffer(bitmap_dump);
        write_to_buffer(attribute_dump);
        write_to_buffer(inode_metadata);

        const auto data_compressed = utils::arithmetic::compress(data_);
        int size = *(int*)(data_compressed.data() + data_compressed.size() - sizeof(int)); // size is at the end of the stream
        // we need to move it to the front
        referenced_inode_->resize(0);
        referenced_inode_->write((char*)&size, sizeof(size), 0);
        referenced_inode_->write((char*)data_compressed.data(), data_compressed.size() - sizeof(int), sizeof(int));
        /// Now, we have written all the metadata:
                /// [LZ4 SIZE]
                /// [LZ4 Compressed metadata]
                /// [DENTRY]
        dentry_start_ = data_compressed.size(); // so save_dentry_unblocked() knows where to continue
        save_dentry_unblocked();

        // relink root
        inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<root_inode_pointer>(new_inode_num_);

        // change old to redundancy
        inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
        inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
    }
    else if (inode_type() == S_IFDIR) // dentry, but not root
    {
        copy_on_write(); // relink

        /// check if child needs a reference
        if (!inode_construct_info_.block_attribute->get<newly_allocated_thus_no_cow>(cow_index))
        {
            // now, we start to change current dentry reference
            const auto ptr = dentry_map_reversed_search_map_.find(cow_index);
            cfs_assert_simple(ptr != dentry_map_reversed_search_map_.end());
            const auto name = ptr->second;
            dentry_map_reversed_search_map_.erase(ptr);
            const auto ptr_ = dentry_map_.find(name);
            cfs_assert_simple(ptr_ != dentry_map_.end());
            dentry_map_.erase(ptr_);

            // copy over
            const auto new_block = inode_construct_info_.block_manager->allocate();
            const auto new_lock = referenced_inode_->lock_page(new_block);
            cfs_assert_simple(content.size() == static_info_->block_size);
            std::memcpy(new_lock->data(), content.data(), content.size());
            // set attributes
            inode_construct_info_.block_attribute->clear(new_block, {
                                                             .block_status = BLOCK_AVAILABLE_TO_MODIFY_0x00,
                                                             .block_type = INDEX_NODE_BLOCK,
                                                             .block_type_cow = 0,
                                                             .allocation_oom_scan_per_refresh_count = 0,
                                                             .newly_allocated_thus_no_cow = 0,
                                                             .index_node_referencing_number = 1,
                                                             .block_checksum = 0,
                                                         });
            // child old block redefined as cow by themselves

            // new pair: name, new_block, relink here
            dentry_map_.emplace(name, new_block);
            dentry_map_reversed_search_map_.emplace(new_block, name);

            // write to dentry
            dentry_start_ = 0;
            referenced_inode_->resize(0); // clear old data
            save_dentry_unblocked(); // write

            // return the new block to child
            return new_block;
        }

        return cow_index; // now reference, return their own CoW
    }
    else // WTF? how did a non-dentry block receive a fucking CoW from its "child," it has no child!
    {
        throw cfs::error::assertion_failed("Non-dentry received CoW notification from non-existing children"); // yeah you check call tree
    }

    return cow_index;
}

cfs::inode_t::inode_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute,
    inode_t *parent_inode)
:
    parent_inode_(parent_inode),
    static_info_(&parent_fs_governor->static_info_)
{
    current_referenced_inode_ = index;
    referenced_inode_ = std::make_unique<cfs_inode_service_t>(index, parent_fs_governor, block_manager, journal, block_attribute);
    inode_construct_info_ = {
        .parent_fs_governor = parent_fs_governor,
        .block_manager = block_manager,
        .journal = journal,
        .block_attribute = block_attribute,
    };
}

void cfs::inode_t::resize(const uint64_t size)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    referenced_inode_->resize(size);
}

uint64_t cfs::inode_t::read(char *data, const uint64_t size, const uint64_t offset)
{
    std::lock_guard lock(operation_mutex_);
    return referenced_inode_->read(data, size, offset);
}

uint64_t cfs::inode_t::write(const char *data, const uint64_t size, const uint64_t offset)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->write(data, size, offset);
}

void cfs::inode_t::chdev(const int dev)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chdev(dev);
}

void cfs::inode_t::chrdev(const dev_t dev)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chrdev(dev);
}

void cfs::inode_t::chmod(const int mode)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chmod(mode);
}

void cfs::inode_t::chown(const int uid, const int gid)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chown(uid, gid);
}

void cfs::inode_t::set_atime(const timespec st_atim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_atim);
}

void cfs::inode_t::set_ctime(const timespec st_ctim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_ctim);
}

void cfs::inode_t::set_mtime(const timespec st_mtim)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->set_atime(st_mtim);
}

cfs::dentry_t::dentry_pairs_t cfs::dentry_t::ls()
{
    // updated from disk and should change sync with the memory so, just read memory
    dentry_pairs_t pairs;
    std::lock_guard lock(operation_mutex_);
    for (const auto& [name, pointer] : dentry_map_) {
        pairs.emplace(name, pointer);
    }
    return pairs;
}

void cfs::dentry_t::unlink(const std::string & name)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    const auto it = dentry_map_.find(name);
    cfs_assert_simple(it != dentry_map_.end());
    const uint64_t inode_pointer = it->second;

    // remove child from dentry
    dentry_map_.erase(it);
    dentry_map_reversed_search_map_.erase(inode_pointer);
    save_dentry_unblocked();

    /// create child inode
    cfs_inode_service_t target(inode_pointer,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute);
    target.resize(0); // remove all data in inode pointers
    inode_construct_info_.block_manager->deallocate(inode_pointer); // remove child inode
}
