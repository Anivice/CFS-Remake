#include "inode.h"

void cfs::inode_t::save_dentry_unblocked()
{
    if (dentry_map_.empty()) {
        referenced_inode_->resize(dentry_start_);
        return;
    }
    std::vector<uint8_t> buffer_;
    buffer_.clear();
    buffer_.reserve(dentry_map_.size() * 32);

    auto save_bytes = [&](const auto data_type) {
        for (int i = 0; i < sizeof(data_type); i++) { // dentry start signature
            buffer_.push_back(((char*)&data_type)[i]);
        }
    };

    for (const auto & [string, inode] : dentry_map_)
    {
        for (const auto c : string) { buffer_.push_back(c); }
        buffer_.push_back(0); // null terminated string TODO: DO NOT ALLOW ANY CREATION OF NULL TERMINATED DENTRY NAME
        save_bytes(inode);
    }

    const auto compressed = utils::arithmetic::compress(buffer_);
    if (dentry_start_ != 0) {
        referenced_inode_->resize(dentry_start_ + compressed.size() + sizeof(cfs_magick_number));
        referenced_inode_->write(reinterpret_cast<const char *>(&cfs_magick_number), sizeof(cfs_magick_number), dentry_start_);
        referenced_inode_->write(reinterpret_cast<const char *>(compressed.data()), compressed.size(), dentry_start_ + sizeof(cfs_magick_number));
    } else {
        uint64_t offset = 0;
        referenced_inode_->resize(sizeof(dentry_start_) + compressed.size() + sizeof(cfs_magick_number));
        dentry_start_ = sizeof(dentry_start_); // write dentry_start first

        offset += referenced_inode_->write(reinterpret_cast<const char *>(&dentry_start_), sizeof(dentry_start_), 0);
        offset += referenced_inode_->write(reinterpret_cast<const char *>(&cfs_magick_number), sizeof(cfs_magick_number), offset);
        referenced_inode_->write(reinterpret_cast<const char *>(compressed.data()), compressed.size(), offset);
    }
}

void cfs::inode_t::read_dentry_unblocked()
{
    if (size_unblocked() < sizeof(uint64_t)) {
        return;
    }

    referenced_inode_->read(reinterpret_cast<char *>(&dentry_start_), sizeof(dentry_start_), 0);
    dentry_map_.clear();
    dentry_map_reversed_search_map_.clear();
    const auto inode_size = size_unblocked();
    const int64_t buffer_size = static_cast<int64_t>(inode_size) - static_cast<int64_t>(dentry_start_);

    if (buffer_size == 0) {
        return;
    }

    if (inode_size < dentry_start_) {
        elog("dentry_start=", dentry_start_, ", but size is ", inode_size, "\n");
        return;
    }

    std::vector<uint8_t> buffer_(buffer_size, 0);
    const auto rSize = referenced_inode_->read(reinterpret_cast<char *>(buffer_.data()), buffer_.size(), dentry_start_);
    cfs_assert_simple(rSize == buffer_.size());

    if (const char * magic = reinterpret_cast<char *>(buffer_.data());
        !!std::memcmp(magic, &cfs_magick_number, sizeof(cfs_magick_number)))
    {
        elog("Designated signature missing!\n");
        return;
    }

    std::vector dentry_data_(buffer_.begin() + sizeof(cfs_magick_number), buffer_.end());
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
                offset = 0;
                name_is_true_ptr_is_false = false;
                return;
            }

            name += c;
        }
        else
        {
            reinterpret_cast<char *>(&ptr)[offset++] = c;
            if (offset == sizeof(uint64_t))
            {
                dentry_map_.emplace(name, ptr);
                dentry_map_reversed_search_map_.emplace(ptr, name);
                name_is_true_ptr_is_false = true;
                offset = 0;
                name.clear();
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

void cfs::inode_t::copy_on_write() // CoW entry
{
    const std::vector<uint8_t> my_data = dump_inode_raw();
    if (parent_inode_ != nullptr)
    {
        const auto new_inode_num_ = parent_inode_->copy_on_write_invoked_from_child(current_referenced_inode_, my_data); // upload
        if (new_inode_num_ != current_referenced_inode_) // I got referenced
        {
            const auto old_ = current_referenced_inode_;
            current_referenced_inode_ = new_inode_num_; // get new inode
            referenced_inode_.reset();
            referenced_inode_ = std::make_unique<cfs_inode_service_t>(new_inode_num_,
                                                                      inode_construct_info_.parent_fs_governor,
                                                                      inode_construct_info_.block_manager,
                                                                      inode_construct_info_.journal,
                                                                      inode_construct_info_.block_attribute); // relocate reference
            referenced_inode_->cfs_inode_attribute->st_ino = current_referenced_inode_;

            if (inode_construct_info_.block_attribute->get<block_status>(old_) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
                inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
            } else {
                inode_construct_info_.block_attribute->dec<index_node_referencing_number>(old_);
            }
        }
    }
    else
    {
        root_cow();
    }
}

void cfs::inode_t::root_cow()
{
    std::vector<uint8_t> data_;
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
    referenced_inode_.reset();
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
    dentry_start_ = data_compressed.size() + sizeof(dentry_start_); // so save_dentry_unblocked() knows where to continue

    offset = 0;

    referenced_inode_->resize(0);
    offset += referenced_inode_->write(reinterpret_cast<char *>(&dentry_start_), sizeof(uint64_t), offset);
    offset += referenced_inode_->write(reinterpret_cast<const char *>(data_compressed.data()), data_compressed.size(), offset);
    /// Now, we have written all the metadata:
    /// [LZ4 SIZE]
    /// [LZ4 Compressed metadata]
    /// [DENTRY]

    save_dentry_unblocked();

    // relink root
    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<root_inode_pointer>(new_inode_num_);
    referenced_inode_->cfs_inode_attribute->st_ino = new_inode_num_;

    // change old to redundancy
    if (inode_construct_info_.block_attribute->get<block_status>(old_) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
        inode_construct_info_.block_attribute->move<block_type, block_type_cow>(old_);
        inode_construct_info_.block_attribute->set<block_type>(old_, COW_REDUNDANCY_BLOCK);
    } else {
        inode_construct_info_.block_attribute->dec<index_node_referencing_number>(old_);
    }
}

uint64_t cfs::inode_t::copy_on_write_invoked_from_child(const uint64_t cow_index, const std::vector<uint8_t> &content)
{
    /// check if child needs a reference
    if (!inode_construct_info_.block_attribute->get<newly_allocated_thus_no_cow>(cow_index)
        || inode_construct_info_.block_attribute->get<block_status>(cow_index) != BLOCK_AVAILABLE_TO_MODIFY_0x00)
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
        referenced_inode_->resize(dentry_start_); // clear old data
        save_dentry_unblocked(); // write

        // return the new block to child
        return new_block;
    }

    return cow_index; // now reference, return their own CoW
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

void cfs::inode_t::chdev(const dev_t dev)
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

void cfs::inode_t::chmod(const mode_t mode)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write(); // relink
    return referenced_inode_->chmod(mode);
}

void cfs::inode_t::chown(const uid_t uid, const gid_t gid)
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

uint64_t cfs::inode_t::size()
{
    std::lock_guard lock(operation_mutex_);
    return size_unblocked();
}

struct stat cfs::inode_t::get_stat()
{
    std::lock_guard lock(operation_mutex_);
    return get_stat_unblocked();
}

cfs::dentry_t::dentry_t(
    const uint64_t index,
    filesystem *parent_fs_governor,
    cfs_block_manager_t *block_manager,
    cfs_journaling_t *journal,
    cfs_block_attribute_access_t *block_attribute,
    inode_t *parent_inode)
: inode_t(index, parent_fs_governor, block_manager, journal, block_attribute, parent_inode)
{
    if (inode_type() != S_IFDIR) {
        throw error::cannot_initialize_dentry_on_non_dentry_inodes();
    }
    std::lock_guard lock(operation_mutex_);
    read_dentry_unblocked();
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

    if (inode_construct_info_.block_attribute->get<block_status>(inode_pointer) == BLOCK_AVAILABLE_TO_MODIFY_0x00)
    {
        /// create child inode
        cfs_inode_service_t target(inode_pointer,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute);
        target.resize(0); // remove all data in inode pointers
        inode_construct_info_.block_manager->deallocate(inode_pointer); // remove child inode
    }
    else {
        // delink blocks one by one
        auto delink_once = [&](const std::vector<uint64_t> & list) {
            std::ranges::for_each(list, [&](const uint64_t pointer) {
                inode_construct_info_.block_attribute->dec<index_node_referencing_number>(pointer);
            });
        };

        cfs_inode_service_t target(inode_pointer,
                inode_construct_info_.parent_fs_governor,
                inode_construct_info_.block_manager,
                inode_construct_info_.journal,
                inode_construct_info_.block_attribute);
        const auto [lv1, lv2, lv3] = target.linearize_all_blocks();
        delink_once(lv1);
        delink_once(lv2);
        delink_once(lv3);
        delink_once({inode_pointer});
    }
}

uint64_t cfs::dentry_t::erase_entry(const std::string &name)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write();
    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple (ptr != dentry_map_.end());
    const auto inode = ptr->second;
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(inode);
    save_dentry_unblocked();
    return inode;
}

void cfs::dentry_t::add_entry(const std::string &name, const uint64_t index)
{
    std::lock_guard lock(operation_mutex_);
    copy_on_write();
    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple (ptr == dentry_map_.end());
    dentry_map_.emplace(name, index);
    dentry_map_reversed_search_map_.emplace(index, name);
    save_dentry_unblocked();
}

void cfs::dentry_t::snapshot(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    copy_on_write();
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    uint64_t new_inode_index = 0;
    {
        auto new_inode = make_inode_unblocked<dentry_t>(name);
        std::vector<uint8_t> root_raw_dump(referenced_inode_->get_stat().st_size);
        referenced_inode_->read(reinterpret_cast<char *>(root_raw_dump.data()), root_raw_dump.size(), 0);
        new_inode.write(reinterpret_cast<char *>(root_raw_dump.data()), root_raw_dump.size(), 0);
        new_inode_index = new_inode.current_referenced_inode_;
    }

    // force CoW updates from now on
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i) && attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
            inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
        }
    }

    // remove all snapshot links in new root shadow copy
    {
        dentry_t new_dentry = dentry_t(new_inode_index,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute,
            this);

        const auto dentry_map_local = dentry_map_; // erase_entry will cause a CoW that will, in turn, modify dentry_map_
        // that means for loop will be reference an object that no longer exists, thus we use a local copy instead of direct global
        // object
        for (const auto & [ptr_name, ptr]: dentry_map_local)
        {
            if (inode_construct_info_.block_attribute->get<block_status>(ptr) == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01
                || !inode_construct_info_.block_manager->blk_at(ptr) /* CoW refresh happened, and this inode was not notified */
                || ptr_name == name) // me
            {
                new_dentry.erase_entry(ptr_name); // CoW is enforced, so any updates should not temper the root
            }
        }

        new_dentry.copy_on_write(); // force at least one copy of CoW

        // update reference
        new_inode_index = new_dentry.current_referenced_inode_;
    }

    // set new inode as snapshot entry point
    inode_construct_info_.block_attribute->set<block_status>(new_inode_index,
                                                             BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01);

    // under current root, add snapshot entry link
    dentry_map_.emplace(name, new_inode_index);
    dentry_map_reversed_search_map_.emplace(new_inode_index, name);
    save_dentry_unblocked();

    // then we mark again to mask any missing ones, and increate link count this time
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i))
        {
            if (attr.block_status == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
                inode_construct_info_.block_attribute->set<block_status>(i, BLOCK_FROZEN_AND_IS_SNAPSHOT_REGULAR_BLOCK_0x02);
            }
            inode_construct_info_.block_attribute->inc<index_node_referencing_number>(i);
        }
    }

    inode_construct_info_.parent_fs_governor->sync(); // sync when snapshot
}

void cfs::dentry_t::revert(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple(ptr != dentry_map_.end());

    // remove all post snapshot changes
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++) {
        if (inode_construct_info_.block_attribute->get<block_status>(i) == BLOCK_AVAILABLE_TO_MODIFY_0x00) {
            inode_construct_info_.block_attribute->move<block_type, block_type_cow>(i);
            inode_construct_info_.block_attribute->set<block_type>(i, COW_REDUNDANCY_BLOCK);
        }
    }

    // we will remove a root, that means everyone has one less snapshot inode reference
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        if (inode_construct_info_.block_manager->blk_at(i)) {
            inode_construct_info_.block_attribute->dec<index_node_referencing_number>(i);
        }
    }

    // backup snapshot entries
    std::vector < std::pair < std::string, uint64_t > > snapshot_entry_list;
    for (const auto & [pointer_name, pointer] : dentry_map_) {
        if (inode_construct_info_.block_attribute->get<block_status>(pointer) == BLOCK_FROZEN_AND_IS_ENTRY_POINT_OF_SNAPSHOTS_0x01) {
            snapshot_entry_list.emplace_back(pointer_name, pointer);
        }
    }

    // get inode
    const uint64_t inode_pointer = ptr->second;
    // make a dentry from that
    current_referenced_inode_ = inode_pointer;
    referenced_inode_.reset();
    referenced_inode_ = std::make_unique<cfs_inode_service_t>(inode_pointer,
        inode_construct_info_.parent_fs_governor,
        inode_construct_info_.block_manager,
        inode_construct_info_.journal,
        inode_construct_info_.block_attribute);

    // reinit dentry
    dentry_map_.clear();
    dentry_map_reversed_search_map_.clear();
    read_dentry_unblocked();
    copy_on_write(); // force a CoW to redirect root

    // add missing snapshot entry link
    for (const auto & [pointer_name, pointer] : snapshot_entry_list)
    {
        if (pointer != current_referenced_inode_) {
            dentry_map_.emplace(pointer_name, pointer);
            dentry_map_reversed_search_map_.emplace(pointer, pointer_name);
        }
    }

    save_dentry_unblocked(); // save on disk

    uint64_t non_cow_blocks = 0;

    // we reverted, created a new root, that means frozen blocks are, again, relinked once more
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i) && attr.block_status != BLOCK_AVAILABLE_TO_MODIFY_0x00) {
            inode_construct_info_.block_attribute->inc<index_node_referencing_number>(i);
            non_cow_blocks += (attr.block_type != 0x00) ? 1 : 0;
        }
    }

    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<allocated_non_cow_blocks>(non_cow_blocks);
    inode_construct_info_.parent_fs_governor->sync(); // sync when snapshot
}

void cfs::dentry_t::delete_snapshot(const std::string &name)
{
    std::lock_guard<std::mutex> lock(operation_mutex_);
    copy_on_write();
    cfs_assert_simple(parent_inode_ == nullptr);
    bool success = false;
    g_transaction(inode_construct_info_.journal, success, GlobalTransaction_Major_SnapshotCreation);

    const auto ptr = dentry_map_.find(name);
    cfs_assert_simple(ptr != dentry_map_.end());
    uint64_t target_index = ptr->second;

    {
        dentry_t dentry = dentry_t(target_index,
            inode_construct_info_.parent_fs_governor,
            inode_construct_info_.block_manager,
            inode_construct_info_.journal,
            inode_construct_info_.block_attribute,
            this); // construct target child
        std::vector<uint8_t> per_snapshot_fs_info(dentry.dentry_start_ - sizeof(uint64_t));
        dentry.read((char*)per_snapshot_fs_info.data(), per_snapshot_fs_info.size(), sizeof(uint64_t));

        auto decompressed_data = cfs::utils::arithmetic::decompress(per_snapshot_fs_info);

        const auto map_size = static_info_->data_table_end - static_info_->data_table_start;

        // venture and iterate through the whole system
        // without bitmap, we have to iterate through by inode
        // but with bitmap, we can simply use that
        class per_snapshot_bitmap_t : public bitmap_base {
        public:
            explicit per_snapshot_bitmap_t(uint8_t * data, const uint64_t size)
            {
                init_data_array = [&](const uint64_t)->bool
                {
                    data_array_ = data;
                    return true;
                };

                init(size);
            }
        } per_snapshot_bitmap(decompressed_data.data(), // bitmap start
            map_size); // particles. bytes are calculated automatically

        dentry.resize(0);

        // clean up
        for (uint64_t i = 0; i < map_size; i++)
        {
            if (per_snapshot_bitmap.get_bit(i) && inode_construct_info_.block_attribute->get<block_type>(i) != COW_REDUNDANCY_BLOCK)
            {
                inode_construct_info_.block_manager->deallocate(i);
                if (inode_construct_info_.block_attribute->get<index_node_referencing_number>(i) == 0)
                {
                    inode_construct_info_.block_attribute->move<block_type, block_type_cow>(i); // backup block type
                    inode_construct_info_.block_attribute->set<block_type>(i, COW_REDUNDANCY_BLOCK); // set it as redundancy
                }
            }
        }

        target_index = dentry.current_referenced_inode_; // update reference
    }

    inode_construct_info_.block_attribute->move<block_type, block_type_cow>(target_index); // backup block type
    inode_construct_info_.block_attribute->set<block_type>(target_index, COW_REDUNDANCY_BLOCK); // set that as redundancy

    // remove corresponding link as well
    dentry_map_.erase(name);
    dentry_map_reversed_search_map_.erase(target_index);
    save_dentry_unblocked();

    uint64_t non_cow_blocks = 0;

    // we reverted, created a new root, that means frozen blocks are, again, relinked once more
    for (uint64_t i = 0; i < static_info_->data_table_end - static_info_->data_table_start; i++)
    {
        const auto attr = inode_construct_info_.block_attribute->get(i);
        if (inode_construct_info_.block_manager->blk_at(i) && attr.block_status != BLOCK_AVAILABLE_TO_MODIFY_0x00) {
            non_cow_blocks += (attr.block_type != 0x00) ? 1 : 0;
        }
    }

    inode_construct_info_.parent_fs_governor->cfs_header_block.set_info<allocated_non_cow_blocks>(non_cow_blocks);
    inode_construct_info_.parent_fs_governor->sync(); // sync
}
